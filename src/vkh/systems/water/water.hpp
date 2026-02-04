#pragma once

#include <glm/geometric.hpp>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <vector>

#include "../../audio.hpp"
#include "../../engineContext.hpp"
#include "../../image.hpp"
#include "../../scene.hpp"
#include "../skybox.hpp"
#include "../system.hpp"
#include "WSTessendorf.hpp"

namespace vkh {
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

    constexpr static VkVertexInputBindingDescription GetBindingDescription() {
      return VkVertexInputBindingDescription{.binding = 0,
                                             .stride = sizeof(Vertex),
                                             .inputRate =
                                                 VK_VERTEX_INPUT_RATE_VERTEX};
    }

    static std::vector<VkVertexInputAttributeDescription>
    GetAttributeDescriptions() {
      return std::vector<VkVertexInputAttributeDescription>{
          {.location = 0,
           .binding = 0,
           .format = VK_FORMAT_R32G32B32_SFLOAT,
           .offset = offsetof(Vertex, pos)},
          {.location = 1,
           .binding = 0,
           .format = VK_FORMAT_R32G32_SFLOAT,
           .offset = offsetof(Vertex, uv)}};
    }

    static const inline std::vector<VkVertexInputBindingDescription>
        bindingDescriptions{GetBindingDescription()};

    static const inline std::vector<VkVertexInputAttributeDescription>
        attribDescriptions{GetAttributeDescriptions()};
  };

  uint32_t tileResolution{32};
  float vertexDistance{WSTessendorf::tileLength /
                       static_cast<float>(tileResolution)};

  VkDescriptorSetLayout setLayout;
  std::vector<VkDescriptorSet> sets;
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
  void updateDescriptorSet(VkDescriptorSet set);
  void updateUniformBuffer();
  void createDescriptorSetLayout();
  void createMesh();
};
} // namespace vkh
