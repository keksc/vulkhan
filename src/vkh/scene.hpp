#pragma once

#include <fastgltf/types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>
#include <vulkan/vulkan.hpp>

#include "AABB.hpp"
#include "buffer.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "image.hpp"

#include <algorithm>
#include <filesystem>
#include <limits>
#include <memory>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

namespace vkh {

template <typename T, typename = void> struct has_pos : std::false_type {};

template <typename T>
struct has_pos<T, std::void_t<decltype(std::declval<T>().pos)>>
    : std::true_type {};

template <typename T, typename = void> struct has_normal : std::false_type {};

template <typename T>
struct has_normal<T, std::void_t<decltype(std::declval<T>().normal)>>
    : std::true_type {};

template <typename T, typename = void> struct has_uv : std::false_type {};

template <typename T>
struct has_uv<T, std::void_t<decltype(std::declval<T>().uv)>> : std::true_type {
};

template <typename T, typename = void> struct has_skinning : std::false_type {};

template <typename T>
struct has_skinning<T, std::void_t<decltype(std::declval<T>().jointWeights),
                                   decltype(std::declval<T>().jointIndices)>>
    : std::true_type {};

template <typename VertexType> struct SceneCreateInfo {
  std::vector<VertexType> &vertices;
  std::vector<uint32_t> &indices;

  // Optional base-color texture. If pixels is empty, the Scene falls back to
  // a solid white 1x1 texture (previous default behavior). Pixels are
  // tightly packed RGBA8, sRGB is NOT applied to them (unlike the fallback
  // white color, which is gamma corrected) since callers are expected to
  // supply already-encoded sRGB bytes.
  std::vector<uint8_t> texturePixels{};
  glm::uvec2 textureSize{0, 0};
};

template <typename VertexType> class Scene {
public:
  struct Skin {
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<size_t> joints;
  };
  struct Mesh {
    AABB aabb{};
    glm::mat4 transform{1.f};
    std::optional<size_t> skinIndex;
    struct Primitive {
      uint32_t indexOffset{};
      uint32_t indexCount{};
      size_t materialIndex;
      void draw(vk::CommandBuffer commandBuffer) {
        commandBuffer.drawIndexed(indexCount, 1, indexOffset, 0, 0);
      }
    };
    std::vector<Primitive> primitives;
  };
  struct Material {
    std::optional<std::size_t> baseColorTextureIndex;
    glm::vec4 baseColorFactor{};
    std::optional<std::size_t> metallicRoughnessTextureIndex;
    float roughnessFactor{};
    glm::vec4 metallicFactor{};
    std::optional<std::size_t> normalTextureIndex;
  };

  static_assert(has_pos<VertexType>::value,
                "VertexType must have a pos member");

  struct Animation {
    struct Sampler {
      fastgltf::AnimationInterpolation interpolation;
      std::vector<float> inputs;          // keyframe times
      std::vector<glm::vec4> outputsVec4; // positions
    };
    struct Channel {
      fastgltf::AnimationPath path; // translation, rotation, scale, or weights
      size_t nodeIndex;             // target node index
      size_t samplerIndex;
    };
    std::vector<Sampler> samplers;
    std::vector<Channel> channels;
    float start = std::numeric_limits<float>::max();
    float end = std::numeric_limits<float>::min();
  };

  struct SceneNode {
    std::optional<size_t> meshIndex;
    std::vector<size_t> children;
    glm::vec3 translation{0.f};
    glm::quat rotation{1.f, 0.f, 0.f, 0.f};
    glm::vec3 scale{1.f, 1.f, 1.f};
    glm::mat4 matrix{1.f};
    bool useMatrix = false;
    glm::mat4 globalTransform{1.f};
  };

  std::vector<SceneNode> nodes;
  std::vector<size_t> rootNodes;
  std::vector<Animation> animations;
  std::vector<Skin> skins;
  fastgltf::Asset gltf;
  std::vector<VertexType> vertices;
  std::vector<uint32_t> indices;

  Scene(EngineContext &context, const std::filesystem::path &path,
        bool disableMaterial = false);
  Scene(EngineContext &context, const SceneCreateInfo<VertexType> &createInfo,
        vk::DescriptorSetLayout setLayout = nullptr);

  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  void bind(EngineContext &context, vk::CommandBuffer commandBuffer,
            vk::PipelineLayout pipelineLayout) {
    vk::Buffer buffers[] = {*vertexBuffer};
    vk::DeviceSize offsets[] = {0};
    commandBuffer.bindVertexBuffers(0, 1, buffers, offsets);
    commandBuffer.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);
  }

  // Declared here, defined in sceneBuilder.hpp: parses the glTF file on the
  // CPU (fills nodes/meshes/animations/skins/vertices/indices).
  void loadModelCPU(const std::filesystem::path &path);

  // Declared here, defined in sceneBuilder.hpp: uploads images/materials and
  // the vertex/index buffers to the GPU.
  void uploadModelGPU(vk::DescriptorSetLayout setLayout);

  void updateNodeTransforms(size_t nodeIndex,
                            const glm::mat4 &parentTransform) {
    auto &node = nodes[nodeIndex];
    glm::mat4 local = node.matrix;

    if (!node.useMatrix) {
      local = glm::translate(glm::mat4(1.f), node.translation) *
              glm::mat4_cast(node.rotation) *
              glm::scale(glm::mat4(1.f), node.scale);
    }

    node.globalTransform = parentTransform * local;

    if (node.meshIndex.has_value()) {
      meshes[node.meshIndex.value()].transform = node.globalTransform;
    }
    for (auto child : node.children) {
      updateNodeTransforms(child, node.globalTransform);
    }
  }

  void updateAnimation(size_t animIndex, float time) {
    if (animIndex >= animations.size())
      return;
    auto &anim = animations[animIndex];

    for (auto &channel : anim.channels) {
      auto &sampler = anim.samplers[channel.samplerIndex];
      if (sampler.inputs.empty())
        continue;

      float t = std::clamp(time, anim.start, anim.end);
      auto it =
          std::upper_bound(sampler.inputs.begin(), sampler.inputs.end(), t);
      size_t nextKey = std::distance(sampler.inputs.begin(), it);
      size_t keyIndex = (nextKey > 0) ? nextKey - 1 : 0;

      if (nextKey >= sampler.inputs.size()) {
        nextKey = sampler.inputs.size() - 1;
      }

      float t1 = sampler.inputs[keyIndex];
      float t2 = sampler.inputs[nextKey];
      float factor = (t2 > t1) ? (t - t1) / (t2 - t1) : 0.0f;

      glm::vec4 v1 = sampler.outputsVec4[keyIndex];
      glm::vec4 v2 = sampler.outputsVec4[nextKey];
      auto &node = nodes[channel.nodeIndex];

      if (channel.path == fastgltf::AnimationPath::Translation) {
        node.translation = glm::mix(glm::vec3(v1), glm::vec3(v2), factor);
      } else if (channel.path == fastgltf::AnimationPath::Rotation) {
        glm::quat q1(v1.w, v1.x, v1.y, v1.z);
        glm::quat q2(v2.w, v2.x, v2.y, v2.z);
        node.rotation = glm::normalize(glm::slerp(q1, q2, factor));
      } else if (channel.path == fastgltf::AnimationPath::Scale) {
        node.scale = glm::mix(glm::vec3(v1), glm::vec3(v2), factor);
      }

      node.useMatrix = false;
    }

    for (auto root : rootNodes) {
      updateNodeTransforms(root, glm::mat4(1.0f));
    }
  }

  std::vector<Image2D> images;
  vk::DescriptorSet sceneTextureSet = nullptr;
  std::vector<Material> materials;

  size_t getIndicesSize() const { return indexCount; }
  size_t getVerticesSize() const { return vertexCount; }

  Buffer<VertexType> &getVertexBuffer() { return *vertexBuffer; }
  Buffer<uint32_t> &getIndexBuffer() { return *indexBuffer; }

  std::vector<Mesh> meshes;

private:
  // Declared here, defined in sceneBuilder.hpp.
  void createBuffers(const std::vector<VertexType> &vertices,
                     const std::vector<uint32_t> &indices);

  EngineContext &context;
  std::unique_ptr<Buffer<VertexType>> vertexBuffer;
  uint32_t vertexCount;
  std::unique_ptr<Buffer<uint32_t>> indexBuffer;
  uint32_t indexCount;
  bool disableMaterial{};
};

} // namespace vkh
