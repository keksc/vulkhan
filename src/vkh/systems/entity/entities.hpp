#pragma once

#include "../../mesh.hpp"
#include "../system.hpp"
#include <algorithm>

namespace vkh {
class EntitySys : public System {
public:
  EntitySys(EngineContext &context);
  ~EntitySys();
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
  struct Transform {
    glm::vec3 position{};
    glm::vec3 scale{1.f, 1.f, 1.f};
    glm::quat orientation{};

    glm::mat4 mat4();
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
    std::unique_ptr<Mesh<Vertex>> mesh;
  };
  void render(std::vector<Entity> &entities);
  void addEntity(std::vector<Entity> &entities, Transform transform,
                 const std::filesystem::path &path, RigidBody rigidBody);

private:
  void createSampler();
  void createSetLayout();

  std::unique_ptr<GraphicsPipeline> pipeline;
  VkSampler sampler;
  std::unique_ptr<DescriptorSetLayout> setLayout;
};
} // namespace vkh
