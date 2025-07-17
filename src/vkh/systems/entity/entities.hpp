#pragma once

#include "../../scene.hpp"
#include "../system.hpp"
#include <algorithm>
#include <memory>

namespace vkh {
class EntitySys : public System {
public:
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

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(Vertex, pos));
      attributeDescriptions.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(Vertex, normal));
      attributeDescriptions.emplace_back(2, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(Vertex, uv));

      return attributeDescriptions;
    }
  };
  struct Transform {
    glm::vec3 position{};
    glm::vec3 scale{1.f, 1.f, 1.f};
    glm::quat orientation{};

    glm::mat4 mat4() const;
    glm::mat3 normalMatrix();
  };
  struct RigidBody {
    glm::vec3 velocity{0.f};
    float mass{1.f};
    const glm::vec3 computeWeight() const { return {0, mass * 9.81f, 0}; }
  };
  struct Entity {
    Transform transform;
    RigidBody rigidBody;
    std::shared_ptr<Scene<Vertex>> scene;
    size_t meshIndex;
  };
  EntitySys(EngineContext &context, std::vector<Entity> &entities);
  ~EntitySys();
  void render();
  std::vector<Entity> &entities;

private:
  void createSampler();

  std::unique_ptr<GraphicsPipeline> pipeline;
  VkSampler sampler;
};
} // namespace vkh
