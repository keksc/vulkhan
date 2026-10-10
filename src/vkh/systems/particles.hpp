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
    glm::vec4 params{};

    static std::vector<vk::VertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(Vertex);
      bindingDescriptions[0].inputRate = vk::VertexInputRate::eInstance;
      return bindingDescriptions;
    }

    static std::vector<vk::VertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, col));

      attributeDescriptions.emplace_back(2, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, params));
      return attributeDescriptions;
    }
  };

  static constexpr uint32_t maxParticles = 8192;
  static constexpr uint32_t maxAttractors = 16;
  static constexpr uint32_t maxPatches = 25;
  void setEnvironment(std::vector<glm::vec3> patches);

  void setAttractor(std::vector<glm::mat4> newTransformations);

private:
  struct PushConstantData {
    uint32_t transformationCount;
    uint32_t patchCount;
    uint32_t particleCount;
    float time;
  };

  struct Frame {
    std::unique_ptr<Buffer<Vertex>> vertices;
    std::unique_ptr<Buffer<glm::mat4>> transforms;
    vk::DescriptorSet set;
  };
  std::vector<Frame> frames;
  std::vector<glm::mat4> attractors;
  std::vector<glm::vec3> patches;
  uint32_t particleCount = 0;

  void createPipeline();
  void createVB();
  void setupDescriptorSetLayout();
  void setupDescriptorSet();

  std::unique_ptr<GraphicsPipeline> graphicsPipeline;
  std::unique_ptr<ComputePipeline> computePipeline;

  vk::DescriptorSetLayout setLayout;
};

} // namespace vkh
