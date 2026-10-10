#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../../AABB.hpp"
#include "../../pipeline.hpp"
#include "../../scene.hpp"
#include "../culling.hpp"
#include "../system.hpp"
#include "instanceSlots.hpp"
#include <unordered_map>

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
    glm::quat orientation{1.f, 0.f, 0.f, 0.f};
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
    std::size_t meshIndex = 0;
    uint32_t ownerSlot = std::numeric_limits<uint32_t>::max();

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
    int32_t groupIndex; // index into the per-(scene,mesh) group arrays used
                        // by the culling compute pass
  };

  using CullingPush = vkh::CullingPush;

  struct SceneBatch {
    std::shared_ptr<Scene<Vertex>> scene;
    uint32_t firstDrawCommandOffset;
    uint32_t drawCommandCount;
  };

  EntitySys(EngineContext &context);
  ~EntitySys();

  using Handle = InstanceHandle;
  using Model = Scene<Vertex>;
  // Main-thread loading, cached by normalized path and material mode.
  std::shared_ptr<Model> loadScene(const std::filesystem::path &path,
                                   bool disableMaterial = false);
  Handle spawn(const std::shared_ptr<Model> &scene, const Transform &transform);
  Handle spawn(const std::shared_ptr<Model> &scene) {
    return spawn(scene, Transform{});
  }
  Handle spawn(const std::filesystem::path &path, const Transform &transform) {
    return spawn(loadScene(path), transform);
  }
  Handle spawn(const std::filesystem::path &path) {
    return spawn(loadScene(path));
  }
  bool destroy(Handle handle);
  bool isAlive(Handle handle) const;
  bool setTransform(Handle handle, const Transform &transform);
  bool setColor(Handle handle, glm::vec4 color);
  bool setId(Handle handle, uint32_t id);
  std::optional<Transform> getTransform(Handle handle) const;
  Handle pickInstance(const Ray &ray, float &distance,
                      float maxDistance = std::numeric_limits<float>::max());

  // Once after beginFrame retires the current fence, before culling/drawing.
  void prepareFrame() {
    updateBuffers();
    updateJoints();
  }
  void updateJoints();
  void cull(vk::CommandBuffer cmd);
  // Re-culls THIS frame's already-built instance/draw-command lists
  // against an arbitrary view-projection matrix instead of the real
  // camera's - e.g. a PlanarReflectionSys's mirrored camera (see
  // PlanarReflectionSys::getReflViewProj()). Overwrites the SAME
  // compactedInstanceBuffers/indirectDrawBuffers[frameIndex] that cull()
  // and render() use - there is only one set of these per frame-in-
  // flight, not one per caller, so whichever of cull()/cullForViewProj()
  // ran MOST RECENTLY this frame is what the next render() call actually
  // draws.
  //
  // CALLER'S RESPONSIBILITY: call this (paired with its own render() call
  // for the reflection target) for every planar reflection BEFORE the
  // real per-frame cull(cmd) call for the frame's actual camera - never
  // after, or the main scene's render() will silently draw with a
  // mirrored-camera's culled set. See main.cpp's reflection-pass loop for
  // the intended ordering.
  void cullForViewProj(vk::CommandBuffer cmd, const glm::mat4 &viewProj);
  // `globalSetOverride`: see SkySys::render()'s identical parameter - same
  // purpose (PlanarReflectionSys's mirrored-camera set), same default
  // (nullptr -> normal per-frame global set, unchanged for existing
  // callers).
  //
  // `clipPlane`: world-space plane (a,b,c,d) pushed to entities.vert's
  // gl_ClipDistance[0] = dot(vec4(worldPos,1), clipPlane) - see
  // PlanarReflectionSys::getClipPlane(). Default (0,1,0,1e9) never clips
  // anything at any plausible world-space height (dot reduces to
  // worldPos.y + 1e9, always positive), so ordinary callers don't need to
  // think about this at all; only the reflection-pass call site in
  // main.cpp passes a real plane.
  //
  // Renders whatever cull()/cullForViewProj() most recently compacted -
  // this method has no camera awareness itself, it just draws the
  // current compactedInstanceBuffers/indirectDrawBuffers[frameIndex]. For
  // a planar reflection's mirrored camera, pair this with a
  // cullForViewProj(cmd, mirroredViewProj) call immediately before it
  // (see cullForViewProj()'s own doc comment on ordering) - otherwise
  // this draws whatever the REAL camera's frustum last culled, which
  // means anything outside the real camera's frustum but visible from
  // the MIRRORED one (a mountain range beside/behind the real camera
  // that the reflection should show) would incorrectly be missing.
  void render(vk::DescriptorSet globalSetOverride = nullptr,
              const glm::vec4 &clipPlane = glm::vec4(0.f, 1.f, 0.f, 1e9f));
  void updateBuffers();

  Entity *pickEntity(const Ray &ray, float &distance,
                     float maxDistance = std::numeric_limits<float>::max());
  Entity *getPointingAt(float maxDistance = 1.0f);

  vk::DescriptorSetLayout texturesSetLayout;
  vk::DescriptorSetLayout instanceSetLayout;

  // Legacy mesh-level access. Do not reorder/erase entries managed by handles.
  std::vector<Entity> entities;

private:
  struct Instance {
    Transform transform;
    std::vector<size_t> meshEntities;
  };
  InstanceSlots<Instance> instances;
  std::unordered_map<std::string, std::weak_ptr<Model>> sceneCache;
  // Retain resources referenced by each frame until its fence is retired.
  std::vector<std::vector<std::shared_ptr<Model>>> frameScenes;
  void createSetLayouts();
  void createPipeline();
  void createCullingPipeline();
  void createFinalizePipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::vector<std::unique_ptr<Buffer<GPUInstanceData>>> instanceBuffers;
  std::vector<std::unique_ptr<Buffer<vk::DrawIndexedIndirectCommand>>>
      indirectDrawBuffers;
  std::vector<std::unique_ptr<Buffer<glm::mat4>>> jointBuffers;
  std::vector<vk::DescriptorSet> instanceDescriptorSets;

  // Compute culling members. Culling runs in two passes sharing one
  // descriptor set layout:
  //  pass 1 (cullingPipeline)  : frustum-cull instances, compact survivors
  //  pass 2 (finalizePipeline) : write each group's visible count into its
  //                              draw command(s)
  vk::DescriptorSetLayout cullingSetLayout = nullptr;
  std::vector<vk::DescriptorSet> cullingDescriptorSets;
  std::unique_ptr<ComputePipeline> cullingPipeline;
  std::unique_ptr<ComputePipeline> finalizePipeline;

  // GPU-only, written by the culling pass and read by the vertex shader.
  std::vector<std::unique_ptr<Buffer<GPUInstanceData>>>
      compactedInstanceBuffers;
  // Static per-group data, rebuilt alongside cpuDrawCommands.
  std::vector<std::unique_ptr<Buffer<uint32_t>>> groupFirstInstanceBuffers;
  std::vector<std::unique_ptr<Buffer<uint32_t>>> commandGroupIndexBuffers;
  // Zeroed by a transfer command before each view; accumulated by culling.
  std::vector<std::unique_ptr<Buffer<uint32_t>>> groupVisibleCountBuffers;

  std::vector<SceneBatch> sceneBatches;
  std::vector<size_t> renderOrder;
  struct FrameEntityData {
    glm::mat4 modelMatrix;
    glm::mat4 normalMatrix;
    AABB worldAABB;
  };
  // Indexed by render order; capacity survives frames to avoid allocations.
  std::vector<FrameEntityData> frameEntityData;

  std::vector<GPUInstanceData> cpuInstanceData;
  std::vector<vk::DrawIndexedIndirectCommand> cpuDrawCommands;
  std::vector<glm::mat4> cpuJointData;
  std::vector<uint32_t>
      cpuGroupFirstInstance; // one entry per group (scene,mesh,primitive
                             // normally, or per LOD level's equivalent -
                             // see EntitySys::updateBuffers())
  std::vector<uint32_t> cpuCommandGroupIndex; // one entry per draw command
  std::vector<bool> framesDirty;
  bool structuralDirty = true;

  void flushBuffers(int frameIndex);

  // Shared compaction/finalization for the camera pushed by the caller.
  void dispatchCulling(vk::CommandBuffer cmd, int frameIndex,
                       const CullingPush &view);

public:
  void markStructuralDirty() { structuralDirty = true; }
};

} // namespace vkh
