#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../../AxisAlignedBoundingBox.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../system.hpp"

namespace vkh {

class GraphicsPipeline;

class EntitySys : public System {
public:
  struct Vertex {
    glm::vec3 pos{};
    glm::vec3 normal{};
    glm::vec2 uv{};
    glm::uvec4 jointIndices{};
    glm::vec4 jointWeights{};

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
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(Vertex, normal));
      attributeDescriptions.emplace_back(2, 0, vk::Format::eR32G32Sfloat,
                                         offsetof(Vertex, uv));
      attributeDescriptions.emplace_back(3, 0, vk::Format::eR32G32B32A32Uint,
                                         offsetof(Vertex, jointIndices));
      attributeDescriptions.emplace_back(4, 0, vk::Format::eR32G32B32A32Sfloat,
                                         offsetof(Vertex, jointWeights));

      return attributeDescriptions;
    }
  };

  struct Transform {
    glm::vec3 position{};
    glm::quat orientation{};
    glm::vec3 scale{1.f, 1.f, 1.f};

    glm::mat4 mat4() const;
    glm::mat3 normalMatrix();
  };

  struct RigidBody {
    glm::vec3 velocity{0.f};
    float mass{1.f};
    const glm::vec3 computeWeight() const {
      return glm::vec3{0, mass * 9.81f, 0};
    }
  };

  struct Entity {
    Transform transform;
    RigidBody rigidBody;
    std::shared_ptr<Scene<Vertex>> scene;
    std::size_t meshIndex;

    const inline Scene<Vertex>::Mesh &getMesh() const {
      return scene->meshes[meshIndex];
    }

    static constexpr uint32_t LOCAL_ENTITY_ID =
        std::numeric_limits<uint32_t>::max();
    uint32_t id = LOCAL_ENTITY_ID;

    glm::vec4 color{1.f, 1.f, 1.f, 1.f};

    AABB getWorldAABB() const;
  };

  struct GPUInstanceData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
    glm::vec4 color;
    glm::vec3 aabbMin;
    int32_t textureIndex; // -1 if no texture
    glm::vec3 aabbMax;
    int32_t metallicRoughnessTextureIndex;
    float roughnessFactor;
    float metallicFactor;
    int32_t jointOffset;
    int32_t isVisible; // 1 if visible, 0 if not
  };

  struct CullingUbo {
    glm::vec4 frustumPlanes[6];
    uint32_t totalInstances;
  };

  struct SceneBatch {
    std::shared_ptr<Scene<Vertex>> scene;
    uint32_t firstDrawCommandOffset;
    uint32_t drawCommandCount;
  };

  EntitySys(EngineContext &context);
  ~EntitySys();

  void updateJoints();
  void cull(vk::CommandBuffer cmd);
  void render();
  void updateBuffers();

  Entity *pickEntity(const Ray &ray, float &distance,
                     float maxDistance = std::numeric_limits<float>::max());
  Entity *getPointingAt(float maxDistance = 1.0f);

  vk::DescriptorSetLayout texturesSetLayout;
  vk::DescriptorSetLayout instanceSetLayout;

  std::vector<Entity> entities;

private:
  void createSetLayouts();
  void createPipeline();
  void createCullingPipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::vector<std::unique_ptr<Buffer<GPUInstanceData>>> instanceBuffers;
  std::vector<std::unique_ptr<Buffer<vk::DrawIndexedIndirectCommand>>>
      indirectDrawBuffers;
  std::vector<std::unique_ptr<Buffer<glm::mat4>>> jointBuffers;
  std::vector<vk::DescriptorSet> instanceDescriptorSets;

  // Compute culling members
  vk::DescriptorSetLayout cullingSetLayout = nullptr;
  std::vector<vk::DescriptorSet> cullingDescriptorSets;
  std::unique_ptr<ComputePipeline> cullingPipeline;
  std::vector<std::unique_ptr<Buffer<CullingUbo>>> cullingUboBuffers;

  std::vector<SceneBatch> sceneBatches;

  std::vector<GPUInstanceData> cpuInstanceData;
  std::vector<vk::DrawIndexedIndirectCommand> cpuDrawCommands;
  std::vector<glm::mat4> cpuJointData;
  std::vector<bool> framesDirty;
  bool structuralDirty = true;

  void flushBuffers(int frameIndex);

public:
  void markStructuralDirty() { structuralDirty = true; }
};

} // namespace vkh
