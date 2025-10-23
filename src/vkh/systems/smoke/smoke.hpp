#pragma once

#include "../../buffer.hpp"
#include "../system.hpp"

#include "fluidGrid.hpp"

namespace vkh {
class SmokeSys : public System {
public:
  SmokeSys(EngineContext &context);
  void update();
  void render();

  struct Vertex {
    glm::vec2 pos{};
    glm::vec3 col{};

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

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(Vertex, col));

      return attributeDescriptions;
    }
  };

  FluidGrid fluidGrid;

private:
  void createPipeline();
  void createBuffer();

  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Buffer<Vertex>> vertexBuffer;

  const unsigned int interpolatedScale = 6;
}; // namespace particlesSys
} // namespace vkh
