#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../image.hpp"
#include "system.hpp"
#include <memory>
#include <vector>

namespace vkh {

class GraphicsPipeline;
template <typename T> class Scene;

class SkyboxSys : public System {
public:
  struct Vertex {
    glm::vec3 pos{};

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

      return attributeDescriptions;
    }
  };

  SkyboxSys(EngineContext &context);
  ~SkyboxSys();

  // This system should be among the first to be rendered
  void render();

  vk::DescriptorSetLayout setLayout;
  vk::DescriptorSet set;

private:
  void createSetLayout();

  std::unique_ptr<GraphicsPipeline> pipeline;
  Image cubeMap;
  std::unique_ptr<Scene<Vertex>> cubeScene;
};

} // namespace vkh
