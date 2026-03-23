#pragma once

#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fastgltf/tools.hpp>
#include <fastgltf/types.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtc/type_ptr.hpp>
#include <glm/gtx/quaternion.hpp>

#include "AxisAlignedBoundingBox.hpp"
#include "buffer.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "image.hpp"

#include <algorithm>
#include <filesystem>
#include <format>
#include <limits>
#include <memory>
#include <type_traits>
#include <utility>
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
};

template <typename VertexType> class Scene {
public:
  struct Skin {
    std::vector<glm::mat4> inverseBindMatrices;
    std::vector<size_t> joints;
  };
  struct Mesh {
    AABB aabb{};
    glm::mat4 transform;
    std::optional<size_t> skinIndex;
    struct Primitive {
      uint32_t indexOffset{};
      uint32_t indexCount{};
      size_t materialIndex;
      void draw(VkCommandBuffer commandBuffer) {
        vkCmdDrawIndexed(commandBuffer, indexCount, 1, indexOffset, 0, 0);
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

  Scene(EngineContext &context, const std::filesystem::path &path,
        VkDescriptorSetLayout setLayout, bool disableMaterial = false)
      : context{context}, disableMaterial{disableMaterial} {
    loadModel(path, setLayout);
  }

  Scene(EngineContext &context, const SceneCreateInfo<VertexType> &createInfo)
      : context{context}, disableMaterial{true} {
    ImageCreateInfo_color imageInfo{};
    imageInfo.size = {1, 1};
    glm::vec4 color = {1.0f, 1.0f, 1.0f, 1.0f}; // White
    color.r = std::pow(color.r, 1.0f / 2.2f);
    color.g = std::pow(color.g, 1.0f / 2.2f);
    color.b = std::pow(color.b, 1.0f / 2.2f);
    uint8_t r = static_cast<uint8_t>(color.r * 255.0f + 0.5f);
    uint8_t g = static_cast<uint8_t>(color.g * 255.0f + 0.5f);
    uint8_t b = static_cast<uint8_t>(color.b * 255.0f + 0.5f);
    uint8_t a = static_cast<uint8_t>(color.a * 255.0f + 0.5f);
    imageInfo.color = (a << 24) | (b << 16) | (g << 8) | r;
    imageInfo.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    imageInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    std::string name = std::format("{:#x} color image", imageInfo.color);
    imageInfo.name = name.c_str();
    images.emplace_back(context, imageInfo);

    createBuffers(createInfo.vertices, createInfo.indices);
  }

  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  void bind(EngineContext &context, VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout) {
    VkBuffer buffers[] = {*vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);
    vkCmdBindIndexBuffer(commandBuffer, *indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  }

  void loadModel(const std::filesystem::path &path,
                 VkDescriptorSetLayout setLayout) {
    auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
    if (gltfFile.error() != fastgltf::Error::None) {
      throw std::runtime_error(
          std::format("Failed to load {}: {}", path.string(),
                      fastgltf::getErrorMessage(gltfFile.error())));
    }
    fastgltf::Parser parser;
    auto asset = parser.loadGltf(gltfFile.get(), path.parent_path(),
                                 fastgltf::Options::LoadExternalBuffers |
                                     fastgltf::Options::LoadExternalImages);
    if (asset.error() != fastgltf::Error::None) {
      throw std::runtime_error(std::format(
          "Failed to parse GLB: {}", fastgltf::getErrorMessage(asset.error())));
    }
    auto gltf = std::move(asset.get());

    std::vector<VertexType> vertices;
    std::vector<uint32_t> indices;

    for (const auto &mesh : gltf.meshes) {
      auto &newMesh = meshes.emplace_back();
      uint32_t start = static_cast<uint32_t>(indices.size());
      for (const auto &primitive : mesh.primitives) {
        auto &newPrimitive = newMesh.primitives.emplace_back();
        newPrimitive.indexOffset = start;
        newPrimitive.materialIndex =
            primitive.materialIndex.value_or(0); // Set material index always
        size_t initial_vtx = vertices.size();

        if (primitive.indicesAccessor.has_value()) {
          fastgltf::Accessor &indexAccessor =
              gltf.accessors[primitive.indicesAccessor.value()];
          indices.reserve(indices.size() + indexAccessor.count);
          if (indexAccessor.componentType ==
              fastgltf::ComponentType::UnsignedInt) {
            fastgltf::iterateAccessor<std::uint32_t>(
                gltf, indexAccessor,
                [initial_vtx, &indices](std::uint32_t idx) {
                  indices.emplace_back(idx +
                                       static_cast<uint32_t>(initial_vtx));
                });
          } else if (indexAccessor.componentType ==
                     fastgltf::ComponentType::UnsignedShort) {
            fastgltf::iterateAccessor<std::uint16_t>(
                gltf, indexAccessor,
                [initial_vtx, &indices](std::uint16_t idx) {
                  indices.emplace_back(static_cast<uint32_t>(idx) +
                                       static_cast<uint32_t>(initial_vtx));
                });
          } else if (indexAccessor.componentType ==
                     fastgltf::ComponentType::UnsignedByte) {
            fastgltf::iterateAccessor<std::uint8_t>(
                gltf, indexAccessor, [initial_vtx, &indices](std::uint8_t idx) {
                  indices.emplace_back(static_cast<uint32_t>(idx) +
                                       static_cast<uint32_t>(initial_vtx));
                });
          } else {
            throw std::runtime_error("Unsupported index component type");
          }
          uint32_t end = static_cast<uint32_t>(indices.size());
          newPrimitive.indexCount = end - start;
          indexCount += indexAccessor.count;
        }

        auto posAttribute = primitive.findAttribute("POSITION");
        if (posAttribute != primitive.attributes.end()) {
          fastgltf::Accessor &posAccessor =
              gltf.accessors[posAttribute->accessorIndex];
          size_t count = posAccessor.count;
          vertices.resize(vertices.size() + count);
          fastgltf::iterateAccessorWithIndex<glm::vec3>(
              gltf, posAccessor,
              [initial_vtx, &vertices, &newMesh](glm::vec3 v, size_t index) {
                vertices[initial_vtx + index].pos = v;
                newMesh.aabb.min = glm::min(newMesh.aabb.min, v);
                newMesh.aabb.max = glm::max(newMesh.aabb.max, v);
              });
        }

        if constexpr (has_normal<VertexType>::value) {
          auto normalAttribute = primitive.findAttribute("NORMAL");
          if (normalAttribute != primitive.attributes.end()) {
            fastgltf::Accessor &normalAccessor =
                gltf.accessors[normalAttribute->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec3>(
                gltf, normalAccessor,
                [initial_vtx, &vertices](glm::vec3 v, size_t index) {
                  vertices[initial_vtx + index].normal = v;
                });
          }
        }

        if constexpr (has_uv<VertexType>::value) {
          auto uvAttribute = primitive.findAttribute("TEXCOORD_0");
          if (uvAttribute != primitive.attributes.end()) {
            fastgltf::Accessor &uvAccessor =
                gltf.accessors[uvAttribute->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec2>(
                gltf, uvAccessor,
                [initial_vtx, &vertices](glm::vec2 v, size_t index) {
                  vertices[initial_vtx + index].uv = v;
                });
          }
        }
        if constexpr (has_skinning<VertexType>::value) {
          auto jointIndicesAttribute = primitive.findAttribute("JOINTS_0");
          if (jointIndicesAttribute != primitive.attributes.end()) {
            fastgltf::Accessor &jointIndicesAccessor =
                gltf.accessors[jointIndicesAttribute->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::uvec4>(
                gltf, jointIndicesAccessor,
                [initial_vtx, &vertices](glm::uvec4 v, size_t index) {
                  vertices[initial_vtx + index].jointIndices = v;
                });
          }
          auto jointWeightsAttribute = primitive.findAttribute("WEIGHTS_0");
          if (jointWeightsAttribute != primitive.attributes.end()) {
            fastgltf::Accessor &jointWeightsAccessor =
                gltf.accessors[jointWeightsAttribute->accessorIndex];
            fastgltf::iterateAccessorWithIndex<glm::vec4>(
                gltf, jointWeightsAccessor,
                [initial_vtx, &vertices](glm::vec4 v, size_t index) {
                  vertices[initial_vtx + index].jointWeights = v;
                });
          }
        }
      }
    }

    nodes.resize(gltf.nodes.size());
    for (size_t i = 0; i < gltf.nodes.size(); ++i) {
      auto &gltfNode = gltf.nodes[i];
      nodes[i].meshIndex = gltfNode.meshIndex;
      if (gltfNode.meshIndex.has_value()) {
        meshes[gltfNode.meshIndex.value()].skinIndex = gltfNode.skinIndex;
      }

      std::visit(
          fastgltf::visitor{[&](fastgltf::math::fmat4x4 matrix) {
                              nodes[i].matrix = glm::make_mat4(matrix.data());
                              nodes[i].useMatrix = true;
                            },
                            [&](fastgltf::TRS trs) {
                              nodes[i].translation =
                                  glm::make_vec3(trs.translation.data());
                              // glTF Quats are X, Y, Z, W. glm::quat
                              // constructor expects W, X, Y, Z.
                              nodes[i].rotation =
                                  glm::quat(trs.rotation[3], trs.rotation[0],
                                            trs.rotation[1], trs.rotation[2]);
                              nodes[i].scale = glm::make_vec3(trs.scale.data());
                              nodes[i].useMatrix = false;
                            }},
          gltfNode.transform);

      for (auto child : gltfNode.children) {
        nodes[i].children.push_back(child);
      }
    }

    // Find the root nodes (nodes without parents) to evaluate the tree
    std::vector<bool> isChild(gltf.nodes.size(), false);
    for (auto &n : nodes) {
      for (auto c : n.children)
        isChild[c] = true;
    }
    for (size_t i = 0; i < isChild.size(); ++i) {
      if (!isChild[i])
        rootNodes.push_back(i);
    }

    for (auto &gltfAnim : gltf.animations) {
      auto &anim = animations.emplace_back();

      for (auto &gltfSampler : gltfAnim.samplers) {
        auto &sampler = anim.samplers.emplace_back();
        sampler.interpolation = gltfSampler.interpolation;

        auto &inputAccessor = gltf.accessors[gltfSampler.inputAccessor];
        sampler.inputs.reserve(inputAccessor.count);
        fastgltf::iterateAccessor<float>(gltf, inputAccessor, [&](float v) {
          sampler.inputs.push_back(v);
          anim.start = std::min(anim.start, v);
          anim.end = std::max(anim.end, v);
        });

        auto &outputAccessor = gltf.accessors[gltfSampler.outputAccessor];
        sampler.outputsVec4.reserve(outputAccessor.count);
        if (outputAccessor.type == fastgltf::AccessorType::Vec3) {
          fastgltf::iterateAccessor<glm::vec3>(
              gltf, outputAccessor, [&](glm::vec3 v) {
                sampler.outputsVec4.emplace_back(v.x, v.y, v.z, 0.0f);
              });
        } else if (outputAccessor.type == fastgltf::AccessorType::Vec4) {
          fastgltf::iterateAccessor<glm::vec4>(
              gltf, outputAccessor,
              [&](glm::vec4 v) { sampler.outputsVec4.push_back(v); });
        }
      }

      for (auto &gltfChannel : gltfAnim.channels) {
        if (!gltfChannel.nodeIndex.has_value())
          continue;
        auto &channel = anim.channels.emplace_back();
        channel.path = gltfChannel.path;
        channel.nodeIndex = gltfChannel.nodeIndex.value();
        channel.samplerIndex = gltfChannel.samplerIndex;
      }

      if (anim.start > anim.end) {
        anim.start = 0.0f;
        anim.end = 0.0f;
      }
    }
    for (const auto &gltfSkin : gltf.skins) {
      auto &skin = skins.emplace_back();
      for (auto joint : gltfSkin.joints) {
        skin.joints.push_back(joint);
      }
      if (gltfSkin.inverseBindMatrices.has_value()) {
        fastgltf::Accessor &ibmAccessor =
            gltf.accessors[gltfSkin.inverseBindMatrices.value()];
        skin.inverseBindMatrices.reserve(ibmAccessor.count);
        fastgltf::iterateAccessor<fastgltf::math::fmat4x4>(
            gltf, ibmAccessor, [&](fastgltf::math::fmat4x4 m) {
              skin.inverseBindMatrices.push_back(glm::make_mat4(m.data()));
            });
      } else {
        skin.inverseBindMatrices.assign(skin.joints.size(), glm::mat4(1.0f));
      }
    }

    // Calculate the initial global transforms before rendering
    for (auto root : rootNodes) {
      updateNodeTransforms(root, glm::mat4(1.0f));
    }

    if (!disableMaterial) {
      for (auto &image : gltf.images) {
        std::visit(
            fastgltf::visitor{
                [](auto &arg) {},
                [&](fastgltf::sources::BufferView &view) {
                  auto &bufferView = gltf.bufferViews[view.bufferViewIndex];
                  auto &buffer = gltf.buffers[bufferView.bufferIndex];
                  std::visit(
                      fastgltf::visitor{
                          [](auto &arg) {},
                          [&](fastgltf::sources::Array &vector) {
                            ImageCreateInfo_PNGdata createInfo;
                            createInfo.data = reinterpret_cast<void *>(
                                vector.bytes.data() + bufferView.byteOffset);
                            createInfo.dataSize =
                                static_cast<size_t>(bufferView.byteLength);
                            std::string name =
                                std::format("image {} for scene {}", image.name,
                                            createInfo.name);
                            images.emplace_back(context, createInfo);
                          }},
                      buffer.data);
                },
            },
            image.data);
      }
      for (auto &material : gltf.materials) {
        Material mat{};
        mat.baseColorFactor =
            glm::make_vec4(material.pbrData.baseColorFactor.data());
        mat.roughnessFactor = material.pbrData.roughnessFactor;
        auto &baseColorTexture = material.pbrData.baseColorTexture;
        auto &roughnessTexture = material.pbrData.metallicRoughnessTexture;
        if (baseColorTexture.has_value()) {
          auto &tex = gltf.textures[baseColorTexture.value().textureIndex];
          mat.baseColorTextureIndex = tex.imageIndex;
        }
        if (roughnessTexture.has_value()) {
          auto &tex = gltf.textures[roughnessTexture.value().textureIndex];
          mat.metallicRoughnessTextureIndex = tex.imageIndex;
        }
        auto &normalTexture = material.normalTexture;
        if (normalTexture.has_value()) {
          auto &tex = gltf.textures[normalTexture.value().textureIndex];
          mat.normalTextureIndex = tex.imageIndex;
        }
        materials.emplace_back(mat);
      }
      Material mat{};
      if (materials.empty()) {
        mat.baseColorFactor = glm::vec4{};
        materials.emplace_back(mat);
      }

      // One descriptor set for the entire scene's texture array
      if (!images.empty()) {
        sceneTextureSet =
            context.vulkan.globalDescriptorAllocator->allocate(setLayout);

        std::vector<VkDescriptorImageInfo> imageInfos;
        imageInfos.reserve(images.size());
        for (auto &img : images) {
          imageInfos.push_back(
              img.getDescriptorInfo(context.vulkan.defaultSampler));
        }

        VkWriteDescriptorSet write{};
        write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dstSet = sceneTextureSet;
        write.dstBinding = 0;
        write.dstArrayElement = 0;
        write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        write.descriptorCount = static_cast<uint32_t>(imageInfos.size());
        write.pImageInfo = imageInfos.data();

        vkUpdateDescriptorSets(context.vulkan.device, 1, &write, 0, nullptr);
      }
    }

    createBuffers(vertices, indices);
  }
  void updateNodeTransforms(size_t nodeIndex,
                            const glm::mat4 &parentTransform) {
    auto &node = nodes[nodeIndex];
    glm::mat4 local = node.matrix;

    // If the node uses TRS components (or is animated), calculate the local
    // matrix
    if (!node.useMatrix) {
      local = glm::translate(glm::mat4(1.f), node.translation) *
              glm::mat4_cast(node.rotation) *
              glm::scale(glm::mat4(1.f), node.scale);
    }

    node.globalTransform = parentTransform * local;

    // Push the final matrix to the actual mesh drawn by the engine
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
      size_t keyIndex = 0;
      for (size_t i = 0; i < sampler.inputs.size() - 1; ++i) {
        if (t <= sampler.inputs[i + 1]) {
          keyIndex = i;
          break;
        }
      }

      size_t nextKey = std::min(keyIndex + 1, sampler.inputs.size() - 1);
      float t1 = sampler.inputs[keyIndex];
      float t2 = sampler.inputs[nextKey];
      float factor =
          (t2 > t1) ? (t - t1) / (t2 - t1) : 0.0f; // Linear interpolation

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

      node.useMatrix =
          false; // Force the node to use our updated TRS components
    }

    for (auto root : rootNodes) {
      updateNodeTransforms(root, glm::mat4(1.0f));
    }
  }

  std::vector<Image> images;
  VkDescriptorSet sceneTextureSet = VK_NULL_HANDLE;
  std::vector<Material> materials;

  size_t getIndicesSize() const { return indexCount; }
  size_t getVerticesSize() const { return vertexCount; }

  std::vector<Mesh> meshes;

private:
  void createBuffers(const std::vector<VertexType> &vertices,
                     const std::vector<uint32_t> &indices) {
    if (vertices.empty() || indices.empty()) {
      throw std::runtime_error(
          "Cannot create buffers with empty vertices or indices");
    }
    indexCount = static_cast<uint32_t>(indices.size());
    vertexCount = static_cast<uint32_t>(vertices.size());

    VkDeviceSize indicesSize = sizeof(uint32_t) * indexCount;
    VkDeviceSize verticesSize = sizeof(VertexType) * vertexCount;

    Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    verticesSize + indicesSize);

    indexBuffer = std::make_unique<Buffer<uint32_t>>(
        context,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexCount);

    vertexBuffer = std::make_unique<Buffer<VertexType>>(
        context,
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, vertexCount);

    stagingBuffer.map();
    stagingBuffer.write(indices.data(), indicesSize);
    stagingBuffer.write(vertices.data(), verticesSize, indicesSize);

    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = 0;
    copyRegion.dstOffset = 0;
    copyRegion.size = indicesSize;

    auto cmd = beginSingleTimeCommands(context);
    vkCmdCopyBuffer(cmd, stagingBuffer, *indexBuffer, 1, &copyRegion);

    copyRegion.srcOffset = indicesSize;
    copyRegion.size = verticesSize;
    vkCmdCopyBuffer(cmd, stagingBuffer, *vertexBuffer, 1, &copyRegion);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

    if (meshes.empty()) {
      auto &newMesh = meshes.emplace_back();
      auto &newPrimitive = newMesh.primitives.emplace_back();
      newPrimitive.indexOffset = 0;
      newPrimitive.indexCount = indexCount;
      newPrimitive.materialIndex = 0;
    }
  }

  EngineContext &context;
  std::unique_ptr<Buffer<VertexType>> vertexBuffer;
  uint32_t vertexCount;
  std::unique_ptr<Buffer<uint32_t>> indexBuffer;
  uint32_t indexCount;
  bool disableMaterial{};
};

} // namespace vkh
