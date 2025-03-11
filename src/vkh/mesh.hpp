#pragma once

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
#include <string>
#include <vector>

namespace vkh {
template <typename T> struct MeshCreateInfo {
  std::vector<T> &vertices;
  std::vector<uint32_t> &indices;
};
template <typename T> class Mesh {
public:
  Mesh(EngineContext &context, const std::filesystem::path &path)
      : context{context} {
    loadModel(path);

    createDescriptors();
  }
  Mesh(EngineContext &context, const MeshCreateInfo<T> &createInfo)
      : context{context} {
    createBuffers(createInfo.vertices, createInfo.indices);
  }

  Mesh(const Mesh &) = delete;
  Mesh &operator=(const Mesh &) = delete;

  void bind(EngineContext &context, VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout,
            std::vector<VkDescriptorSet> descriptorSets = {}) {
    VkBuffer buffers[] = {*vertexBuffer};
    VkDeviceSize offsets[] = {0};
    vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

    vkCmdBindIndexBuffer(commandBuffer, *indexBuffer, 0, VK_INDEX_TYPE_UINT32);
    if (!descriptorSets.empty()) {
      vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                              pipelineLayout, 0, descriptorSets.size(),
                              descriptorSets.data(), 0, nullptr);
    }
  }
  void draw(VkCommandBuffer commandBuffer) {
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
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

    // Loop over all meshes and their primitives.
    for (const auto &mesh : gltf.meshes) {
      for (const auto &primitive : mesh.primitives) {
        size_t initial_vtx = vertices.size();

        if (primitive.indicesAccessor.has_value()) {
          fastgltf::Accessor &indexAccessor =
              gltf.accessors[primitive.indicesAccessor.value()];
          indices.reserve(indices.size() + indexAccessor.count);
          fastgltf::iterateAccessor<std::uint32_t>(
              gltf, indexAccessor, [initial_vtx, &indices](std::uint32_t idx) {
                indices.push_back(idx + static_cast<uint32_t>(initial_vtx));
              });
        }

        auto posAttribute = primitive.findAttribute("POSITION");
        if (posAttribute == primitive.attributes.end()) {
          throw std::runtime_error("Mesh primitive has no POSITION attribute");
        }
        {
          fastgltf::Accessor &posAccessor =
              gltf.accessors[posAttribute->accessorIndex];
          size_t count = posAccessor.count;
          // Increase vertices for this primitive.
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
    }

    if (gltf.images.empty()) {
      throw std::runtime_error(
          fmt::format("no image in GLB {}", path.string()));
    }
    auto &image = gltf.images[0];
    std::visit(
        fastgltf::visitor{
            [](auto &arg) {},
            [&](fastgltf::sources::BufferView &view) {
              auto &bufferView = gltf.bufferViews[view.bufferViewIndex];
              auto &buffer = gltf.buffers[bufferView.bufferIndex];
              // Yes, we've already loaded every buffer into some GL buffer.
              // However, with GL it's simpler to just copy the buffer data
              // again for the texture. Besides, this is just an example.
              std::visit(
                  fastgltf::visitor{
                      // We only care about VectorWithMime here, because we
                      // specify LoadExternalBuffers, meaning
                      // all buffers are already loaded into a vector.
                      [](auto &arg) {},
                      [&](fastgltf::sources::Array &vector) {
                        int width, height, nrChannels;
                        texture = std::make_shared<Image>(
                            context,
                            reinterpret_cast<const void *>(
                                vector.bytes.data() + bufferView.byteOffset),
                            static_cast<int>(bufferView.byteLength));
                      }},
                  buffer.data);
            },
        },
        image.data);

    createBuffers(vertices, indices);
  }

  std::shared_ptr<Image> texture;

  VkDescriptorSet textureDescriptorSet;

  size_t getIndicesSize() const {return indexCount;}
  size_t getVerticesSize() const {return vertexCount;}

private:
  void createBuffers(const std::vector<T> &vertices,
                     const std::vector<uint32_t> &indices) {
    indexCount = indices.size();
    vertexCount = vertices.size();

    VkDeviceSize indicesSize = sizeof(uint32_t) * indexCount;
    VkDeviceSize verticesSize = sizeof(T) * vertexCount;

    BufferCreateInfo bufInfo{};
    bufInfo.instanceSize = verticesSize + indicesSize;
    bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                               VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
    Buffer stagingBuffer{context, bufInfo};

    bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

    bufInfo.instanceSize = sizeof(uint32_t)*indexCount;
    bufInfo.usage =
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    indexBuffer = std::make_unique<Buffer>(context, bufInfo);

    bufInfo.instanceSize = sizeof(T)*vertexCount;
    bufInfo.usage =
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
    vertexBuffer = std::make_unique<Buffer>(context, bufInfo);

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
  }
  void createDescriptors() {
    auto descriptorInfo = texture->getDescriptorInfo();
    DescriptorWriter(*context.vulkan.modelDescriptorSetLayout,
                     *context.vulkan.globalDescriptorPool)
        .writeImage(0, &descriptorInfo)
        .build(textureDescriptorSet);
  }

  EngineContext &context;

  std::unique_ptr<Buffer> vertexBuffer;
  uint32_t vertexCount;

  std::unique_ptr<Buffer> indexBuffer;
  uint32_t indexCount;
};
} // namespace vkh
