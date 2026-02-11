#pragma once

#include "../../scene.hpp"
#include "../skybox.hpp"
#include "../system.hpp"
#include <algorithm>
#include <memory>
#include <vector>

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
    std::size_t meshIndex;

    inline Scene<Vertex>::Mesh &getMesh() { return scene->meshes[meshIndex]; }
  };

  struct GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
  };

  struct GPUMaterialData {
    glm::vec4 baseColorFactor{1.f};
    int32_t textureIndex; // -1 if no texture
    uint32_t padding[3];
  };

  struct IndirectBatch {
    std::shared_ptr<Scene<Vertex>> scene;
    size_t materialIndex;
    uint32_t indirectOffset;
    bool useTexture;
    glm::vec4 baseColorFactor;
  };

  EntitySys(EngineContext &context);
  ~EntitySys();

  void setEntities(std::vector<Entity> &entities);
  void render();

  VkDescriptorSetLayout textureSetLayout;
  VkDescriptorSetLayout instanceSetLayout;

private:
  void createSetLayouts();
  void createPipeline();
  void updateBuffers(std::vector<Entity> &sortedEntities);

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::unique_ptr<Buffer<GPUInstanceData>> instanceBuffer;
  std::unique_ptr<Buffer<VkDrawIndexedIndirectCommand>> indirectDrawBuffer;
  
  std::vector<IndirectBatch> renderBatches;

  VkDescriptorSet instanceDescriptorSet;
  VkDescriptorSet dummyTextureSet;
};

} // namespace vkh
