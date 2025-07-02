#pragma once

#include "../buffer.hpp"
#include "system.hpp"

namespace vkh {
class ParticleSys : public System {
public:
  ParticleSys(EngineContext &context);
  void update();
  void render();

  struct Vertex {
    glm::vec3 pos{};
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

      attributeDescriptions.emplace_back(
          0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(
          1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, col));

      return attributeDescriptions;
    }
  };

  struct Particle {
    glm::vec3 pos{};
    glm::vec3 col{};
    glm::vec3 velocity;
    float timeOfDeath{1.f};
  };

  std::vector<Particle> particles;
private:

  void createPipeline();
  void createBuffer();

  const int maxParticles = 10000;

  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Buffer<Vertex>> vertexBuffer;
}; // namespace particlesSys
} // namespace vkh
