#pragma once

#include "../system.hpp"

namespace vkh {
class EntitySys : public System {
public:
  EntitySys(EngineContext &context);
  struct Vertex {
    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec2 uv{};

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
      attributeDescriptions.push_back(
          {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});

      return attributeDescriptions;
    }
  };
  void render();

private:
  std::unique_ptr<GraphicsPipeline> pipeline;
};
} // namespace vkh
