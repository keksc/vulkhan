#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../buffer.hpp"
#include "system.hpp"

#include <memory>
#include <vector>

namespace vkh {

class GraphicsPipeline;
class ComputePipeline;
template <typename T> class Buffer;

class ParticleSys : public System {
public:
  ParticleSys(EngineContext &context);
  ~ParticleSys();
  void update();
  void render();

  struct Vertex {
    glm::vec4 pos{};
    glm::vec4 col{};

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

      attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, col));

      return attributeDescriptions;
    }
  };

  static const size_t maxParticles = 2'097'152;

  void setAttractor(std::vector<glm::mat4> newTransformations);

private:
  struct PushConstantData {
    uint32_t transformationCount;
  };

  uint32_t transformationCount = 0;

  std::unique_ptr<Buffer<glm::mat4>> transformationsBuffer;

  void createPipeline();
  void createVB();
  void setupDescriptorSetLayout();
  void setupDescriptorSet();

  std::unique_ptr<GraphicsPipeline> graphicsPipeline;
  std::unique_ptr<ComputePipeline> computePipeline;

  std::unique_ptr<Buffer<Vertex>> vertexBuffer;
  vk::DescriptorSetLayout setLayout;
  vk::DescriptorSet set;
};

} // namespace vkh
