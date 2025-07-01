#pragma once

#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <glm/glm.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "image.hpp"

#include <filesystem>
#include <format>
#include <memory>
#include <print>
#include <type_traits>
#include <utility>
#include <vector>

namespace vkh {

// Trait to check if VertexType has a 'pos' member
template <typename T, typename = void> struct has_pos : std::false_type {};

template <typename T>
struct has_pos<T, std::void_t<decltype(std::declval<T>().pos)>>
    : std::true_type {};

// Trait to check if VertexType has a 'normal' member
template <typename T, typename = void> struct has_normal : std::false_type {};

template <typename T>
struct has_normal<T, std::void_t<decltype(std::declval<T>().normal)>>
    : std::true_type {};

// Trait to check if VertexType has a 'uv' member
template <typename T, typename = void> struct has_uv : std::false_type {};

template <typename T>
struct has_uv<T, std::void_t<decltype(std::declval<T>().uv)>> : std::true_type {
};

template <typename VertexType> struct SceneCreateInfo {
  std::vector<VertexType> &vertices;
  std::vector<uint32_t> &indices;
};

// TODO: improve img support
template <typename VertexType> class Scene {
public:
  struct Mesh {
    uint32_t indexOffset{};
    uint32_t indexCount{};
    glm::mat4 transform;
    size_t materialIndex;
    void draw(VkCommandBuffer commandBuffer) {
      vkCmdDrawIndexed(commandBuffer, indexCount, 1, indexOffset, 0, 0);
    }
  };
  struct Material {
    size_t baseColorTextureIndex;
  };
  static_assert(has_pos<VertexType>::value,
                "VertexType must have a pos member");

  Scene(EngineContext &context, const std::filesystem::path &path,
        bool disableMaterial = false)
      : context{context}, disableMaterial{disableMaterial} {
    loadModel(path);
  }
  Scene(EngineContext &context, const SceneCreateInfo<VertexType> &createInfo)
      : context{context}, disableMaterial{true} {
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

  void loadModel(const std::filesystem::path &path) {
    auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
    if (gltfFile.error() != fastgltf::Error::None) {
      throw std::runtime_error(
          std::format("failed to load {}: {}", path.string(),
                      fastgltf::getErrorMessage(gltfFile.error())));
    }
    fastgltf::Parser parser;
    auto asset = parser.loadGltfBinary(gltfFile.get(), path.parent_path(),
                                       fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None) {
      throw std::runtime_error(std::format(
          "Failed to parse GLB: {}", fastgltf::getErrorMessage(asset.error())));
    }
    auto gltf = std::move(asset.get());

    std::vector<VertexType> vertices;
    std::vector<uint32_t> indices;

    for (const auto &mesh : gltf.meshes) {
      Mesh newMesh{};
      uint32_t start = static_cast<uint32_t>(indices.size());
      newMesh.indexOffset = start;
      for (const auto &primitive : mesh.primitives) {
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
                  indices.push_back(idx + static_cast<uint32_t>(initial_vtx));
                });
          } else if (indexAccessor.componentType ==
                     fastgltf::ComponentType::UnsignedShort) {
            fastgltf::iterateAccessor<std::uint16_t>(
                gltf, indexAccessor,
                [initial_vtx, &indices](std::uint16_t idx) {
                  indices.push_back(static_cast<uint32_t>(idx) +
                                    static_cast<uint32_t>(initial_vtx));
                });
          } else if (indexAccessor.componentType ==
                     fastgltf::ComponentType::UnsignedByte) {
            fastgltf::iterateAccessor<std::uint8_t>(
                gltf, indexAccessor, [initial_vtx, &indices](std::uint8_t idx) {
                  indices.push_back(static_cast<uint32_t>(idx) +
                                    static_cast<uint32_t>(initial_vtx));
                });
          } else {
            throw std::runtime_error("Unsupported index component type");
          }
          indexCount += indexAccessor.count;
        }

        auto posAttribute = primitive.findAttribute("POSITION");
        {
          fastgltf::Accessor &posAccessor =
              gltf.accessors[posAttribute->accessorIndex];
          size_t count = posAccessor.count;
          vertices.resize(vertices.size() + count);
          fastgltf::iterateAccessorWithIndex<glm::vec3>(
              gltf, posAccessor,
              [initial_vtx, &vertices](glm::vec3 v, size_t index) {
                vertices[initial_vtx + index].pos = v;
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
        if (!primitive.materialIndex.has_value())
          std::println("No material index, using default 0");
        newMesh.materialIndex = primitive.materialIndex.value_or(0);
      }
      uint32_t end = static_cast<uint32_t>(indices.size());
      newMesh.indexCount = end - start;
      meshes.push_back(newMesh);
    }

    fastgltf::iterateSceneNodes(
        gltf, 0, fastgltf::math::fmat4x4(),
        [&](fastgltf::Node &node, fastgltf::math::fmat4x4 matrix) {
          if (node.meshIndex.has_value()) {
            std::memcpy(&meshes[*node.meshIndex].transform, &matrix,
                        sizeof(glm::mat4));
          }
        });

    if (disableMaterial) {
    } else if (gltf.images.empty()) {
      ImageCreateInfo imageInfo{};
      imageInfo.w = imageInfo.h = 1;
      imageInfo.color = 0x0;
      imageInfo.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.usage =
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      images.emplace_back(context, imageInfo);
    } else {
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
                            auto &image = images.emplace_back(
                                context,
                                reinterpret_cast<void *>(vector.bytes.data() +
                                                         bufferView.byteOffset),
                                static_cast<size_t>(bufferView.byteLength));
                            VkDescriptorSet set;
                            auto descriptorInfo = image.getDescriptorInfo(
                                context.vulkan.defaultSampler);
                            DescriptorWriter(
                                *context.vulkan.sceneDescriptorSetLayout,
                                *context.vulkan.globalDescriptorPool)
                                .writeImage(0, &descriptorInfo)
                                .build(set);
                            imageDescriptorSets.push_back(set);
                          }},
                      buffer.data);
                },
            },
            image.data);
      }
      for (auto &material : gltf.materials) {
        Material mat{};
        if (material.pbrData.baseColorTexture.has_value()) {
          auto &texInfo = material.pbrData.baseColorTexture.value();
          auto &tex = gltf.textures[texInfo.textureIndex];
          mat.baseColorTextureIndex = tex.imageIndex.value_or(0);
        }
        materials.push_back(mat);
      }
    }

    createBuffers(vertices, indices);
  }

  std::vector<Image> images;
  std::vector<VkDescriptorSet> imageDescriptorSets;
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

    meshes.push_back({0, indexCount});
  }

  EngineContext &context;

  std::unique_ptr<Buffer<VertexType>> vertexBuffer;
  uint32_t vertexCount;

  std::unique_ptr<Buffer<uint32_t>> indexBuffer;
  uint32_t indexCount;

  bool disableMaterial{};
};

} // namespace vkh
