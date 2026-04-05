#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include "system.hpp"

#include <memory>

namespace vkh {
class ComputePipeline;
template <typename T> class Buffer;
class GraphicsPipeline;
class ParticleSys : public System {
public:
  ParticleSys(EngineContext &context);
  ~ParticleSys();
  void update();
  void render();

  struct Vertex {
    glm::vec4 pos{};
    glm::vec4 col{};

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

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, VK_FORMAT_R32G32B32A32_SFLOAT,
                                         offsetof(Vertex, col));

      return attributeDescriptions;
    }
  };

  static const size_t maxParticles = 65'536;

private:
  struct PushConstantData {
    glm::vec3 attractorPos{};
    float dt{};
  };

  void createPipeline();
  void createBuffer();
  void setupDescriptors();

  std::unique_ptr<GraphicsPipeline> graphicsPipeline;
  std::unique_ptr<ComputePipeline> computePipeline;

  std::unique_ptr<Buffer<Vertex>> vertexBuffer;
  VkDescriptorSetLayout setLayout;
  VkDescriptorSet set;
}; // namespace particlesSys
} // namespace vkh
