#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../sky.hpp"
#include "../system.hpp"
#include "WSTessendorf.hpp"

#include <cstddef>
#include <memory>
#include <vector>

namespace vkh {

template <typename T> class Buffer;
template <typename T> class Scene;
class GraphicsPipeline;
class EngineContext;

// One placement of a water surface - a single lake patch. Multiple
// WaterSys instances can share the same WSTessendorf (see below), so
// spawning N lakes costs N small meshes/descriptor sets, not N wave sims.
struct WaterCreateInfo {
  // World-space center of the patch. Y is the water level - this is what
  // lets lakes sit at different heights.
  glm::vec3 center{0.f};

  // World-space XZ footprint of the patch - pass the basin's bounding box
  // (e.g. Lake::worldSize).
  glm::vec2 size{WSTessendorf::tileLength};

  float waveScale{0.08f};

  float chopScale{0.3f};

  float animSpeed{1.f};

  // How many times the displacement/foam texture repeats across this
  // patch. The grid mesh is a fixed unit quad scaled by `size`, so a
  // small lake needs a small repeat count (e.g. size / 40) to avoid
  // either stretching one giant wave across it or looking flat. Requires
  // the water sampler to use eRepeat addressing (see createSampler()).
  glm::vec2 uvRepeat{1.f, 1.f};

  // Optional per-cell footprint mask, matching a maskResolution.x by
  // maskResolution.y grid over this patch's `size` bounding box - when
  // non-empty, only cells marked true are included in the generated mesh
  // (see createGridIndices()), so the water surface can match an
  // irregular, non-rectangular basin shape instead of always filling the
  // whole rectangle. Empty (the default) means "no mask - fill the whole
  // rectangle", preserving plain rectangular behavior for any caller with
  // no basin shape to provide (e.g. a hand-placed decorative pond).
  //
  // Indexed [z * maskResolution.x + x] - this is exactly the layout
  // vkh::mountain::Lake::cellMask is rasterized in, and passing a lake's
  // mask straight through here is the intended use (see main.cpp's lake-
  // creation loop). maskResolution MUST equal (tileResolution,
  // tileResolution) below - createGridIndices() checks this and treats a
  // mismatch as "render nothing" rather than reading out of bounds or
  // silently misaligning, so a stale/wrong-sized mask fails safe and
  // visibly instead of silently producing a subtly wrong mesh.
  std::vector<bool> cellMask;
  glm::uvec2 maskResolution{0, 0};

  // NOTE: wind is intentionally NOT a field here - see
  // setGlobalWindDirection()/setGlobalWindStrength() below. Every lake
  // reads the same global wind every frame, the same way every lake
  // already shares one WSTessendorf sim - there's no per-lake override,
  // so lakes can't silently drift out of sync with each other.
};

class WaterSys : public System {
public:
  // The mesh's base grid resolution, before any tessellation shader
  // refinement (see water.tesc).
  static constexpr uint32_t tileResolution = 32;

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

private:
  SkySys &skySys;
  WSTessendorf &modelTess;
  WaterCreateInfo createInfo;

  struct Vertex {
    glm::vec3 pos;
    glm::vec2 uv;

    Vertex(const glm::vec3 &position, const glm::vec2 &texCoord)
        : pos(position), uv(texCoord) {}

    constexpr static vk::VertexInputBindingDescription GetBindingDescription() {
      return vk::VertexInputBindingDescription{0, sizeof(Vertex),
                                               vk::VertexInputRate::eVertex};
    }

    static std::vector<vk::VertexInputAttributeDescription>
    GetAttributeDescriptions() {
      return std::vector<vk::VertexInputAttributeDescription>{
          {0, 0, vk::Format::eR32G32B32Sfloat, offsetof(Vertex, pos)},
          {1, 0, vk::Format::eR32G32Sfloat, offsetof(Vertex, uv)}};
    }

    static const inline std::vector<vk::VertexInputBindingDescription>
        bindingDescriptions{GetBindingDescription()};
    static const inline std::vector<vk::VertexInputAttributeDescription>
        attribDescriptions{GetAttributeDescriptions()};
  };

  vk::DescriptorSetLayout setLayout;
  vk::Sampler repeatSampler; // eRepeat, not context.vulkan.defaultSampler
  std::vector<vk::DescriptorSet> sets;
  std::vector<Buffer<std::byte>> uniformBuffers;
  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Scene<Vertex>> scene;

  bool playAnimation{true};

  struct VertexUBO {
    alignas(16) glm::mat4 model;
    alignas(4) float scale{1.0f}; // waveScale, see WaterCreateInfo
    alignas(8) glm::vec2 uvRepeat{1.f, 1.f};
    alignas(4) float time{0.f};
    alignas(4) float chopScale{0.3f};
    alignas(16) glm::mat4 reflViewProj{1.f};
  } vertexUBO{};

  // Non-owning, may be nullptr - see setPlanarReflectionSource(). Nullptr
  // (or Planar mode selected with no source ever set) is handled in
  // update() by forcing this instance's reflectionMode to SSR regardless
  // of the global mode, rather than binding an unset/garbage descriptor -
  // see update()'s own comment.
  class PlanarReflectionSys *planarReflection{nullptr};

  const uint32_t totalVertexCount = (tileResolution + 1) * (tileResolution + 1);
  const uint32_t indicesPerQuad = 4;
  const uint32_t totalIndexCount =
      tileResolution * tileResolution * indicesPerQuad;

  void createPipeline();
  void createSampler();
  void createUniformBuffers(const uint32_t bufferCount);
  void createDescriptorSets(const uint32_t count);
  std::vector<Vertex> createGridVertices();
  std::vector<uint32_t> createGridIndices();
  void updateDescriptorSet(vk::DescriptorSet set);
  void updateUniformBuffer();
  void createDescriptorSetLayout();
  void createMesh();

  // Thin forwarders to `planarReflection` (see setPlanarReflectionSource())
  // - only called when it's known non-null (see update()'s usePlanar
  // check), kept as separate methods purely so water.cpp doesn't need
  // PlanarReflectionSys's full interface inlined at every call site.
  VkDescriptorImageInfo getPlanarReflectionImageInfo() const;
  glm::mat4 getPlanarReflectionViewProj() const;
};

} // namespace vkh
