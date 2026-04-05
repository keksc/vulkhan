#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include "../image.hpp"
#include "system.hpp"

#include <memory>

namespace vkh {
class GraphicsPipeline;
template <typename T> class Scene;
class SkyboxSys : public System {
public:
  struct Vertex {
    glm::vec3 pos{};

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

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(Vertex, pos));

      return attributeDescriptions;
    }
  };
  SkyboxSys(EngineContext &context);
  ~SkyboxSys();
  // This system should be the first to be rendered
  void render();

  VkDescriptorSetLayout setLayout;
  VkDescriptorSet set;

private:
  void createSetLayout();

  std::unique_ptr<GraphicsPipeline> pipeline;
  Image cubeMap;
  std::unique_ptr<Scene<Vertex>> cubeScene;
};
} // namespace vkh
