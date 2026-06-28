#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../skybox.hpp"
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

class WaterSys : public System {
public:
  WaterSys(EngineContext &context, SkyboxSys &skyboxSys);
  ~WaterSys();
  void prepare();
  void createRenderData();
  void render();
  void update();
  void downloadDisplacementAtWorldPos();

private:
  SkyboxSys &skyboxSys;

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

  uint32_t tileResolution{32};
  float vertexDistance{WSTessendorf::tileLength /
                       static_cast<float>(tileResolution)};

  vk::DescriptorSetLayout setLayout;
  std::vector<vk::DescriptorSet> sets;
  std::vector<Buffer<std::byte>> uniformBuffers;
  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Scene<Vertex>> scene;

  WSTessendorf modelTess;

  bool playAnimation{true};
  float animSpeed{3.0};

  struct VertexUBO {
    alignas(16) glm::mat4 model;
    alignas(4) float scale{1.0f};
  } vertexUBO{};

  const uint32_t totalVertexCount = (tileResolution + 1) * (tileResolution + 1);
  const uint32_t indicesPerQuad = 4;
  const uint32_t totalIndexCount =
      tileResolution * tileResolution * indicesPerQuad;

  void createPipeline();
  void createUniformBuffers(const uint32_t bufferCount);
  void createDescriptorSets(const uint32_t count);
  std::vector<Vertex> createGridVertices();
  std::vector<uint32_t> createGridIndices();
  void updateDescriptorSet(vk::DescriptorSet set);
  void updateUniformBuffer();
  void createDescriptorSetLayout();
  void createMesh();
};

} // namespace vkh
