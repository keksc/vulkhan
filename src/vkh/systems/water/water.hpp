#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../sky.hpp"
#include "../system.hpp"
#include "WSTessendorf.hpp"

#include <cstddef>
#include <cstdint>
#include <memory>
#include <vector>

namespace vkh {

template <typename T> class Buffer;
template <typename T> class Scene;
class GraphicsPipeline;
class EngineContext;

// One placement of a water surface - a single lake. Multiple WaterSys
// instances can share the same WSTessendorf, so spawning N lakes costs N small
// meshes/descriptor sets, not N wave sims.
struct WaterCreateInfo {
  // World-space center of the patch. Y is the water level - this is what
  // lets lakes sit at different heights.
  glm::vec3 center{0.f};

  // World-space XZ bounding box of the surface (pass Lake::worldCenter /
  // Lake::worldSize). It is only used to normalise the mesh into the local
  // [-0.5, 0.5] space the shaders expect (model = translate(center) *
  // scale(size)); it no longer decides what gets drawn.
  glm::vec2 size{WSTessendorf::tileLength};

  float waveScale{0.08f};

  float chopScale{0.3f};

  float animSpeed{1.f};

  // How many times the displacement/foam texture repeats across the bounding
  // box. A small lake needs a small repeat count (e.g. size / 15). Requires
  // the water sampler to use eRepeat addressing (see createShared()).
  glm::vec2 uvRepeat{1.f, 1.f};

  // The water surface itself: world-space XZ lattice points and four indices
  // per quad (v0=(x,z), v1=(x+1,z), v2=(x+1,z+1), v3=(x,z+1)); WaterSys
  // splits each quad into two triangles. All points are at the water level (`center.y`), so only XZ is
  // stored. vkh::mountain::Lake::meshXZ / meshIndices are built in exactly
  // this layout from the lake's connected, flood-filled water, so the surface
  // follows the real shore and never covers unconnected dips; terrain above
  // the water level hides any overshoot through the depth test.
  //
  // Malformed input (index count not a multiple of 4, an index out of
  // range, or meshDepth not matching meshXZ) renders nothing rather than reading out of bounds. Empty = nothing
  // to draw. Both buffers are grown on demand, per frame slot.
  std::vector<glm::vec2> meshXZ;
  // Water depth per point (0 on the shore). The wave displacement is faded to
  // 0 as it goes to 0 (over `shoreFadeDepth`), so the surface meets the
  // terrain exactly. Same size as meshXZ.
  std::vector<float> meshDepth;
  // World-space size of one lattice cell the mesh was built on (the vertex
  // spacing); water.vert uses it to pick the wave texture's mip level.
  float meshCellSize{1.5f};
  std::vector<uint32_t> meshIndices;

  // Depth (world units) over which displacement and chop ramp from 0 at the
  // shore to full strength.
  float shoreFadeDepth{2.f};

  // NOTE: wind is intentionally NOT a field here - every lake reads the same
  // global wind every frame, the same way every lake shares one WSTessendorf
  // sim.
};

class WaterSys : public System {
public:
  WaterSys(EngineContext &context, SkySys &skySys, WSTessendorf &tess,
           const WaterCreateInfo &createInfo = {});
  ~WaterSys();
  void createRenderData();
  void render();

  void setPlanarReflectionSource(class PlanarReflectionSys *planarRefl) {
    planarReflection = planarRefl;
  }

  // If `driveSimulation` is true, also steps the shared WSTessendorf's
  // wave simulation this frame - exactly one WaterSys instance among
  // those sharing a `tess` should be given this responsibility (default:
  // true, so a single-lake setup keeps working unchanged). Pass false
  // for every additional instance (lakes) sharing that same sim.
  void update(bool driveSimulation = true);
  void downloadDisplacementAtWorldPos();

  // Re-targets this instance at a different lake (new surface mesh, level,
  // wave settings) WITHOUT recreating its pipeline, sampler, descriptor sets
  // or uniform buffers - only the mesh is rebuilt. Lets streaming code recycle
  // retired instances instead of constructing new ones (descriptor sets are
  // never freed back to the global allocator, so constructing/destroying a
  // WaterSys per lake would slowly exhaust the pool). The new mesh is
  // uploaded per frame slot on the next update() of that slot, so frames still
  // in flight keep using the old data. update() must run again before
  // render() for each frame slot, and the planar reflection source must be set
  // again afterwards.
  void reconfigure(const WaterCreateInfo &createInfo);

private:
  SkySys &skySys;
  WSTessendorf &modelTess;
  WaterCreateInfo createInfo;

  // Everything that is identical for every lake: descriptor layout, sampler
  // and pipeline (shader load + compile used to run once per lake, a visible
  // hitch whenever one streamed in). Created by the first WaterSys, freed with
  // the last.
  struct Shared;
  std::shared_ptr<Shared> shared;

  struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;
    float shoreDepth; // 0 on the shore, see WaterCreateInfo::meshDepth

    Vertex(const glm::vec3 &position, const glm::vec2 &texCoord, float depth)
        : pos(position), uv(texCoord), shoreDepth(depth) {}

    constexpr static vk::VertexInputBindingDescription GetBindingDescription() {
      return vk::VertexInputBindingDescription{0, sizeof(Vertex),
                                               vk::VertexInputRate::eVertex};
    }

    static std::vector<vk::VertexInputAttributeDescription>
    GetAttributeDescriptions() {
      return std::vector<vk::VertexInputAttributeDescription>{
          {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)},
          {1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)},
          {2, 0, vk::Format::eR32Sfloat, offsetof(Vertex, shoreDepth)}};
    }

    static const inline std::vector<vk::VertexInputBindingDescription>
        bindingDescriptions{GetBindingDescription()};
    static const inline std::vector<vk::VertexInputAttributeDescription>
        attribDescriptions{GetAttributeDescriptions()};
  };

  std::vector<vk::DescriptorSet> sets;
  std::vector<Buffer<std::byte>> uniformBuffers;
  // Per-instance mesh data. The vertex/index lists stay on the CPU (see
  // `vertices` / `indices`) and are uploaded lazily into one pair of buffers
  // per frame slot (see syncMesh()), so reconfigure() never rewrites data a
  // frame in flight may still be reading. The buffers grow to fit and are
  // never referenced by a descriptor set.
  struct FrameMesh {
    std::unique_ptr<Buffer<std::byte>> vertexBuffer;
    std::unique_ptr<Buffer<std::byte>> indexBuffer;
    vk::DeviceSize vertexCap{0}; // bytes allocated
    vk::DeviceSize indexCap{0};
    uint32_t indexCount{0};
    uint64_t version{0}; // meshVersion last uploaded into this slot
  };
  std::vector<FrameMesh> frameMeshes;
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  uint64_t meshVersion{1};
  // Last image views written per frame slot, so update() only calls
  // vkUpdateDescriptorSets when the reflection target actually changed.
  struct BoundImages {
    VkImageView disp{VK_NULL_HANDLE};
    VkImageView refl{VK_NULL_HANDLE};
    bool uboWritten{false};
  };
  std::vector<BoundImages> bound;

  bool playAnimation{true};

  // Layout must keep matching the VertexUBO blocks in water.vert / .frag
  // (the fragment shader declares a prefix of it).
  struct VertexUBO {
    alignas(16) glm::mat4 model;
    alignas(4) float scale{1.0f}; // waveScale, see WaterCreateInfo
    alignas(8) glm::vec2 uvRepeat{1.f, 1.f};
    alignas(4) float time{0.f};
    alignas(4) float chopScale{0.3f};
    // Depth over which wave displacement fades to 0 at the shore (see
    // WaterCreateInfo::shoreFadeDepth). Sits where coverageRes used to.
    alignas(4) float shoreFadeDepth{2.f};
    // Vertex spacing of the lake mesh (WaterCreateInfo::meshCellSize).
    alignas(4) float cellWorld{1.5f};
    alignas(16) glm::mat4 reflViewProj{1.f};
  } vertexUBO{};

  // Non-owning, may be nullptr - see setPlanarReflectionSource(). Nullptr
  // (or Planar mode selected with no source ever set) is handled in
  // update() by forcing this instance's reflectionMode to SSR regardless
  // of the global mode, rather than binding an unset/garbage descriptor.
  class PlanarReflectionSys *planarReflection{nullptr};

  void createShared();
  void createUniformBuffers(const uint32_t bufferCount);
  void createDescriptorSets(const uint32_t count);
  void updateDescriptorSet(vk::DescriptorSet set);
  void updateUniformBuffer();
  void createMesh();
  void syncMesh();

  // Thin forwarders to `planarReflection` (see setPlanarReflectionSource())
  // - only called when it's known non-null.
  VkDescriptorImageInfo getPlanarReflectionImageInfo() const;
  glm::mat4 getPlanarReflectionViewProj() const;
};

} // namespace vkh
