#pragma once

#include "system.hpp"
#include <algorithm>
#include <memory>
#include <ktx.h>
#include <ktxvulkan.h>

namespace vkh {
class SkyboxSys : public System {
public:
  struct Vertex {
    glm::vec3 pos{};
    glm::vec3 normal{};

    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(Vertex);
      bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescriptions;
    }
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.push_back(
          {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos)});
      attributeDescriptions.push_back(
          {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});

      return attributeDescriptions;
    }
  };
  SkyboxSys(EngineContext &context);
  ~SkyboxSys();
  void render();

private:
  void createSampler();
  void loadAsset();
  void createSetLayout();

  std::unique_ptr<GraphicsPipeline> pipeline;
  VkSampler sampler;
  std::shared_ptr<DescriptorSetLayout> setLayout;
};
} // namespace vkh
