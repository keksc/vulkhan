#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

#include "../../AABB.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
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
  struct TreeVariant {
    std::shared_ptr<Scene<Vertex>> scene;
    std::vector<bool> meshIsFoliage;
  };

  // One placed tree - mirrors vkh::mountain::TreePlacement but is TreeSys's
  // own type so this header doesn't depend on mountain.hpp. Construct these
  // directly from TreePlacement's fields at the call site (see main.cpp).
  struct TreePlacement {
    glm::vec3 position{};
    float yaw{};   // radians, random facing around Y
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
    uint32_t flags; // see InstanceFlagBits
    uint32_t groupIndex; // index into the per-(variant,mesh) group arrays
                        // used by the culling compute pass - identical
                        // purpose to EntitySys::GPUInstanceData::groupIndex.
    float _pad0;
  };

  struct CullingUbo {
    glm::vec4 frustumPlanes[6];
    uint32_t totalInstances;
    uint32_t totalCommands;
  };

  // Updated once per frame in updateBuffers(); read by tree.vert for wind
  // sway. Kept separate from CullingUbo (compute-only) since this is a
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
  };

  TreeSys(EngineContext &context);
  ~TreeSys();

  // Registers a tree species. Returns the variant index to use as
  // TreePlacement::variant. Call every addVariant() before the first
  // setPlacements() - placements reference variants by index, and
  // buildInstances() (called from setPlacements()) resolves that index
  // immediately.
  uint32_t addVariant(TreeVariant variant);

  // Replaces the full placement list and rebuilds the CPU-side instance
  // list, per-(variant,mesh) groups, and indirect draw commands from
  // scratch. Trees are treated as static once placed - there is no
  // per-instance update path - so this is meant to be called once (or
  // rarely, e.g. on world regeneration), not per frame.
  void setPlacements(std::vector<TreePlacement> placements);

  void cull(vk::CommandBuffer cmd);
  // Re-culls this frame's already-built instance/draw-command lists
  // against an arbitrary view-projection - same contract and same
  // "call before the real cull()" ordering requirement as
  // EntitySys::cullForViewProj(); see that method's doc comment.
  void cullForViewProj(vk::CommandBuffer cmd, const glm::mat4 &viewProj);
  void render(vk::DescriptorSet globalSetOverride = nullptr,
             const glm::vec4 &clipPlane = glm::vec4(0.f, 1.f, 0.f, 1e9f));
  void updateBuffers(); // uploads instance/draw data once (first frame

  float windStrength = 0.25f;
  glm::vec2 windDir = glm::normalize(glm::vec2(1.f, 0.3f));

  vk::DescriptorSetLayout texturesSetLayout;
  vk::DescriptorSetLayout instanceSetLayout;

private:
  void createSetLayouts();
  void createPipeline();
  void createCullingPipeline();
  void createFinalizePipeline();
  void buildInstances(); // (re)builds cpu* vectors from variants_/placements_
  void flushBuffers(int frameIndex);
  void dispatchCulling(vk::CommandBuffer cmd, int frameIndex);

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::vector<TreeVariant> variants_;
  std::vector<TreePlacement> placements_;

  std::vector<std::unique_ptr<Buffer<GPUInstanceData>>> instanceBuffers;
  std::vector<std::unique_ptr<Buffer<vk::DrawIndexedIndirectCommand>>>
      indirectDrawBuffers;
  std::vector<std::unique_ptr<Buffer<WindUbo>>> windBuffers;
  std::vector<vk::DescriptorSet> instanceDescriptorSets;

  vk::DescriptorSetLayout cullingSetLayout = nullptr;
  std::vector<vk::DescriptorSet> cullingDescriptorSets;
  std::unique_ptr<ComputePipeline> cullingPipeline;
  std::unique_ptr<ComputePipeline> finalizePipeline;
  std::vector<std::unique_ptr<Buffer<CullingUbo>>> cullingUboBuffers;

  std::vector<std::unique_ptr<Buffer<GPUInstanceData>>>
      compactedInstanceBuffers;
  std::vector<std::unique_ptr<Buffer<uint32_t>>> groupFirstInstanceBuffers;
  std::vector<std::unique_ptr<Buffer<uint32_t>>> commandGroupIndexBuffers;
  std::vector<std::unique_ptr<Buffer<uint32_t>>> groupVisibleCountBuffers;

  std::vector<MeshGroupBatch> sceneBatches;

  std::vector<GPUInstanceData> cpuInstanceData;
  std::vector<vk::DrawIndexedIndirectCommand> cpuDrawCommands;
  std::vector<uint32_t> cpuGroupFirstInstance; // one entry per (variant,mesh)
                                              // group
  std::vector<uint32_t> cpuCommandGroupIndex; // one entry per draw command
  std::vector<bool> framesDirty;
};

} // namespace vkh
