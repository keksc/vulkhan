#pragma once

#include <fmt/core.h>
#include <mutex>
#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <fmt/format.h>
#include <glm/glm.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "image.hpp"

#include <filesystem>
#include <memory>
#include <vector>

namespace vkh {
template <typename T> struct MeshCreateInfo {
  std::vector<T> &vertices;
  std::vector<uint32_t> &indices;
};
template <typename T> class Scene {
public:
  Scene(EngineContext &context, const std::filesystem::path &path,
        VkSampler sampler, DescriptorSetLayout &setLayout)
      : context{context} {
    loadModel(path);
    createDescriptors(sampler, setLayout);
  }
  Scene(EngineContext &context, const MeshCreateInfo<T> &createInfo)
      : context{context} {
    createBuffers(createInfo.vertices, createInfo.indices);
  }

  Scene(const Scene &) = delete;
  Scene &operator=(const Scene &) = delete;

  void bind(EngineContext &context, VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            std::vector<VkDescriptorSet> descriptorSets = {}) {
    VkBuffer buffers[] = {*vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer, *indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    if (!descriptorSets.empty()) {
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipelineLayout, 0,
                              static_cast<uint32_t>(descriptorSets.size()),
                              descriptorSets.data(), 0, nullptr);
    }
  }
  void loadModel(const std::filesystem::path &path) {
    auto gltfFile = fastgltf::GltfDataBuffer::FromPath(path);
    if (gltfFile.error() != fastgltf::Error::None) {
      throw std::runtime_error(
          fmt::format("failed to load {}: {}", path.string(),
                      fastgltf::getErrorMessage(gltfFile.error())));
    }
    fastgltf::Parser parser;
    auto asset = parser.loadGltfBinary(gltfFile.get(), path.parent_path(),
                                       fastgltf::Options::None);
    if (asset.error() != fastgltf::Error::None) {
      throw std::runtime_error(fmt::format(
          "Failed to parse GLB: {}", fastgltf::getErrorMessage(asset.error())));
    }
    auto gltf = std::move(asset.get());

    std::vector<T> vertices;
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
                T newVertex;
                newVertex.pos = v;
                vertices[initial_vtx + index] = newVertex;
              });
        }

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

        auto uvAttribute = primitive.findAttribute("TEXCOORD_0");
        if (uvAttribute != primitive.attributes.end()) {
          fastgltf::Accessor &uvAccessor =
              gltf.accessors[uvAttribute->accessorIndex];
          fastgltf::iterateAccessorWithIndex<glm::vec2>(
              gltf, uvAccessor,
              [initial_vtx, &vertices](glm::vec2 v, size_t index) {
                vertices[initial_vtx + index].uv.x = v.x;
                vertices[initial_vtx + index].uv.y = v.y;
              });
        }
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

    if (gltf.images.empty()) {
      ImageCreateInfo imageInfo{};
      imageInfo.w = imageInfo.h = 1;
      imageInfo.color = 0x0;
      imageInfo.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.usage =
          VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
      texture = std::make_shared<Image>(context, imageInfo);
    } else {
      auto &image = gltf.images[0];
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
                          texture = std::make_shared<Image>(
                              context,
                              reinterpret_cast<void *>(
                                  vector.bytes.data() + bufferView.byteOffset),
                              static_cast<size_t>(bufferView.byteLength));
                        }},
                    buffer.data);
              },
          },
          image.data);
    }

    createBuffers(vertices, indices);
  }

  std::shared_ptr<Image> texture;

  VkDescriptorSet textureDescriptorSet;

  size_t getIndicesSize() const { return indexCount; }
  size_t getVerticesSize() const { return vertexCount; }

  auto begin() { return meshes.begin(); }
  auto end() { return meshes.end(); }
  auto begin() const { return meshes.begin(); }
  auto end() const { return meshes.end(); }

private:
  struct Mesh {
    uint32_t indexOffset{};
    uint32_t indexCount{};
    glm::mat4 transform;
    void draw(VkCommandBuffer commandBuffer) {
      vkCmdDrawIndexed(commandBuffer, indexCount, 1, indexOffset, 0, 0);
    }
  };
  std::vector<Mesh> meshes;

  void createBuffers(const std::vector<T> &vertices,
                     const std::vector<uint32_t> &indices) {
    if (vertices.empty() || indices.empty()) {
      throw std::runtime_error(
          "Cannot create buffers with empty vertices or indices");
    }
    indexCount = static_cast<uint32_t>(indices.size());
    vertexCount = static_cast<uint32_t>(vertices.size());

    VkDeviceSize indicesSize = sizeof(uint32_t) * indexCount;
    VkDeviceSize verticesSize = sizeof(T) * vertexCount;

    Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    verticesSize + indicesSize);

    indexBuffer = std::make_unique<Buffer<uint32_t>>(
        context,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, indexCount);

    vertexBuffer = std::make_unique<Buffer<T>>(
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
  void createDescriptors(VkSampler sampler, DescriptorSetLayout &setLayout) {
    auto descriptorInfo = texture->getDescriptorInfo(sampler);
    DescriptorWriter(setLayout, *context.vulkan.globalDescriptorPool)
        .writeImage(0, &descriptorInfo)
        .build(textureDescriptorSet);
  }

  EngineContext &context;

  std::unique_ptr<Buffer<T>> vertexBuffer;
  uint32_t vertexCount;

  std::unique_ptr<Buffer<uint32_t>> indexBuffer;
  uint32_t indexCount;
};
} // namespace vkh
