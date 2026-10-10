#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

#include "../../AABB.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../culling.hpp"
#include "../system.hpp"

namespace vkh {

class GraphicsPipeline;

// GPU-driven instanced tree renderer: static (non-skinned) meshes, culled
// and compacted on the GPU exactly like EntitySys (two-pass compute:
// frustum-cull + compact, then broadcast visible counts into indirect draw
// commands), but with a lighter per-instance/per-vertex format since trees
// never need joints, and a dedicated pipeline that shades trunk and foliage
// meshes differently from a single instance's `flags` bit rather than a
// wholly separate system per mesh type.
//
// Why this exists instead of pushing trees through EntitySys: EntitySys's
// Vertex carries jointIndices/jointWeights for skinned characters - dead
// weight (32 bytes/vertex) on every static tree vertex - and its shader has
// no concept of two-sided/back-lit foliage shading or wind sway. Trees also
// benefit from being their own system because canopies want alpha-to-
// coverage + two-sided lighting while trunks want neither; branching on a
// per-instance flag in one shared pipeline keeps that a single draw-call
// family instead of two entirely separate pipelines per tree part.
class TreeSys : public System {
public:
  struct Vertex {
    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    // r: wind stiffness in [0,1], authored (vertex-painted) per mesh - 0 at
    //    a trunk's base/thick branches (rigid), rising toward 1 at leaf
    //    tips (full sway). Meshes with no authored stiffness default to
    //    solid black (0,0,0,1) which reads as "rigid" - i.e. trunks need no
    //    special-casing, they just don't move.
    // g,b: unused, reserved (e.g. per-vertex AO or hue variation).
    // a: unused, always 1.
    glm::vec4 color{0.f, 0.f, 0.f, 1.f};

    static std::vector<vk::VertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(Vertex);
      bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
      return bindingDescriptions;
    }

    static std::vector<vk::VertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{};
      attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(Vertex, normal));
      attributeDescriptions.emplace_back(2, 0, vk::Format::eR32G32Sfloat,
                                         offsetof(Vertex, uv));
      attributeDescriptions.emplace_back(3, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, color));
      return attributeDescriptions;
    }
  };

  // Bit flags for GPUTreeInstanceData::flags.
  enum InstanceFlagBits : uint32_t {
    eRock = 1u << 1,    // opaque procedural mineral material; no wind
    eFoliage = 1u << 0, // two-sided + back-transmission + stronger wind in
                        // tree.frag/tree.vert; unset = opaque single-sided
                        // trunk/branch shading.
  };

  // One tree species/model: a scene (trunk + canopy meshes, typically 2-4
  // meshes total across LODs) plus which of its meshes are foliage vs
  // solid wood. Indexed 1:1 with scene->meshes - meshIsFoliage[i]
  // corresponds to scene->meshes[i]. Sized to scene->meshes.size() at
  // addVariant() time; a mismatched size is a caller bug (asserted).
  //
  // Meshes aren't self-describing here (no glTF material inspection) so
  // the caller states this explicitly - typically "everything glb-authored
  // as a leaf/needle cluster or billboard card is foliage, everything else
  // (trunk, branches, bark) isn't".
  // Which render() calls draw a variant (see render()'s passMask). Lets one
  // TreeSys hold several LOD tiers of the same species and pick per pass:
  // e.g. a high-poly copy for the main view only, a low-poly copy for both
  // the main view and planar reflections, and a low-poly copy that only
  // reflections draw (stands in for the high-poly tree that is near the
  // camera in the main view).
  enum PassBits : uint32_t {
    ePassMain = 1u << 0,
    ePassReflection = 1u << 1,
    ePassAll = ePassMain | ePassReflection,
  };

  struct TreeVariant {
    std::shared_ptr<Scene<Vertex>> scene;
    std::vector<bool> meshIsFoliage;
    uint32_t passMask{ePassAll};
    uint32_t flags{0};
  };

  // One placed tree - mirrors vkh::mountain::TreePlacement but is TreeSys's
  // own type so this header doesn't depend on mountain.hpp. Construct these
  // directly from TreePlacement's fields at the call site (see main.cpp).
  struct TreePlacement {
    glm::vec3 position{};
    float yaw{}; // radians, random facing around Y
    float scale{1.f};
    uint32_t variant{0}; // index into TreeSys::addVariant() calls, in order
  };

  struct GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
    glm::vec3 aabbMin;
    int32_t textureIndex; // -1 if no texture
    glm::vec3 aabbMax;
    int32_t metallicRoughnessTextureIndex;
    float roughnessFactor;
    float metallicFactor;
    // Per-instance phase offset [0, 2pi) so a forest doesn't sway in
    // lockstep - added to time before the wind sine in tree.vert.
    float windPhase;
    uint32_t flags;      // see InstanceFlagBits
    uint32_t groupIndex; // index into the per-(variant,mesh) group arrays
                         // used by the culling compute pass - identical
                         // purpose to EntitySys::GPUInstanceData::groupIndex.
    float _pad0;
    float _pad1;
    float _pad2;
  };
  static_assert(sizeof(GPUInstanceData) % 16 == 0,
                "GPUInstanceData must match std430 array stride");
  static_assert(sizeof(GPUInstanceData) == 192, "shader struct stride");

  using CullingPush = vkh::CullingPush;

  // Updated once per frame in updateBuffers(); read by tree.vert for wind
  // sway. Kept separate from CullingPush (compute-only) since this is a
  // graphics-stage binding.
  struct WindUbo {
    float time;
    float strength;
    glm::vec2 dir;
  };

  struct MeshGroupBatch {
    std::shared_ptr<Scene<Vertex>> scene;
    uint32_t firstDrawCommandOffset;
    uint32_t drawCommandCount;
    uint32_t passMask;
  };

  // Everything derived from one placement list: the instance array (grouped
  // by (variant, primitive), the layout the culling shaders expect), the
  // per-group bookkeeping and the indirect draw commands. Immutable once
  // built, so it can be produced on a worker thread (buildInstanceSet) and
  // handed to the render thread (setInstanceSet) without locking.
  struct InstanceSet {
    std::vector<GPUInstanceData> instances;
    std::vector<vk::DrawIndexedIndirectCommand> commands;
    std::vector<uint32_t> groupFirstInstance; // one entry per group
    std::vector<uint32_t> commandGroupIndex;  // one entry per draw command
    std::vector<MeshGroupBatch> batches;      // sorted by scene, see build
  };

  TreeSys(EngineContext &context);
  ~TreeSys();

  // Registers a tree species. Returns the variant index to use as
  // TreePlacement::variant. Call every addVariant() before the first
  // setPlacements() - placements reference variants by index, and
  // buildInstances() (called from setPlacements()) resolves that index
  // immediately.
  uint32_t addVariant(TreeVariant variant);

  // Replaces the full placement list (synchronous convenience wrapper over
  // buildInstanceSet + setInstanceSet). For streaming forests build the set
  // on a worker thread instead and call setInstanceSet() when it is done.
  void setPlacements(std::vector<TreePlacement> placements);

  // Thread-safe (reads only the variants registered before the first call,
  // which must not change afterwards). Cost is one pass over `placements`.
  std::shared_ptr<const InstanceSet>
  buildInstanceSet(std::span<const TreePlacement> placements) const;

  // Render-thread only. Cheap: just swaps a pointer; each frame-in-flight
  // slot picks the new set up in its next updateBuffers() and uploads it to
  // the GPU once (staging copy into device-local memory - the culling
  // passes then read VRAM instead of host memory every pass).
  void setInstanceSet(std::shared_ptr<const InstanceSet> set);

  uint32_t instanceCount() const {
    return current_ ? static_cast<uint32_t>(current_->instances.size()) : 0u;
  }

  void cull(vk::CommandBuffer cmd);
  // Re-culls this frame's already-built instance/draw-command lists
  // against an arbitrary view-projection - same contract and same
  // "call before the real cull()" ordering requirement as
  // EntitySys::cullForViewProj(); see that method's doc comment.
  void cullForViewProj(vk::CommandBuffer cmd, const glm::mat4 &viewProj);
  // Draws every batch whose variant's passMask intersects `passMask`.
  void render(vk::DescriptorSet globalSetOverride = nullptr,
              const glm::vec4 &clipPlane = glm::vec4(0.f, 1.f, 0.f, 1e9f),
              uint32_t passMask = ePassMain);
  // Once per frame, before cull()/render(): adopts a newly set instance set
  // for this frame slot, (re)sizes buffers only when capacity is exceeded,
  // and refreshes the wind UBO.
  void updateBuffers();

  vk::DescriptorSetLayout texturesSetLayout;
  vk::DescriptorSetLayout instanceSetLayout;
  // Immutable/thread-safe callback sampled when streamed instances are built.
  std::function<glm::vec3(glm::vec3)> foliageTint;
  std::function<glm::vec3(glm::vec3)> rockTint;

private:
  void createSetLayouts();
  void createPipeline();
  void createCullingPipeline();
  void createFinalizePipeline();
  void ensureBuffers(int frameIndex);
  void uploadInstances(vk::CommandBuffer cmd, int frameIndex);
  void dispatchCulling(vk::CommandBuffer cmd, int frameIndex,
                       const CullingPush &view);

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::vector<TreeVariant> variants_;

  // Latest set handed to setInstanceSet().
  std::shared_ptr<const InstanceSet> current_;

  // Per frame-in-flight slot. A slot only ever touches its own buffers, at a
  // point where its previous frame has retired, so replacing/rewriting them
  // needs no extra synchronisation.
  struct Slot {
    // The set this slot's buffers/descriptors currently describe. Used by
    // every cull()/render() in the frame, so a setInstanceSet() landing
    // mid-frame cannot desynchronise them.
    std::shared_ptr<const InstanceSet> set;
    bool uploaded{true}; // raw instances copied to VRAM for `set`?

    // Raw (pre-cull) instances: device-local, filled through `staging`.
    std::unique_ptr<Buffer<GPUInstanceData>> rawInstances;
    // Transient: created for an upload, dropped at the slot's next
    // updateBuffers() (its copy has retired by then).
    std::unique_ptr<Buffer<GPUInstanceData>> staging;
    std::unique_ptr<Buffer<GPUInstanceData>> compacted;
    std::unique_ptr<Buffer<vk::DrawIndexedIndirectCommand>> indirect;
    std::unique_ptr<Buffer<uint32_t>> groupFirstInstance;
    std::unique_ptr<Buffer<uint32_t>> commandGroupIndex;
    std::unique_ptr<Buffer<uint32_t>> groupVisibleCount;
    std::unique_ptr<Buffer<WindUbo>> wind;

    vk::DescriptorSet instanceSet = nullptr; // allocated once per slot
    vk::DescriptorSet cullingSet = nullptr;  // allocated once per slot

    uint32_t instanceCap{0}, drawCap{0}, groupCap{0};
  };
  std::vector<Slot> slots_;

  std::unique_ptr<ComputePipeline> cullingPipeline;
  std::unique_ptr<ComputePipeline> finalizePipeline;
  vk::DescriptorSetLayout cullingSetLayout = nullptr;
};

} // namespace vkh
