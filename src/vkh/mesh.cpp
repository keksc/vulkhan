#include "mesh.hpp"
#include <memory>
#include <stdexcept>

#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
#include <vulkan/vulkan_core.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <fmt/color.h>
#include <glm/gtx/hash.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "deviceHelpers.hpp"
#include "image.hpp"

#include <cassert>
#include <cstring>

namespace vkh {
void Model::createBuffers(const std::vector<Vertex> &vertices,
                          const std::vector<uint32_t> &indices) {
  indexCount = indices.size();
  vertexCount = vertices.size();

  VkDeviceSize indicesSize = sizeof(uint32_t) * indexCount;
  VkDeviceSize verticesSize = sizeof(Vertex) * vertexCount;

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize =
      verticesSize + indicesSize;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer{context, bufInfo};

  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;

  bufInfo.instanceSize = sizeof(uint32_t);
  bufInfo.instanceCount = indexCount;
  bufInfo.usage =
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);

  bufInfo.instanceSize = sizeof(Vertex);
  bufInfo.instanceCount = vertexCount;
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
void Model::draw(VkCommandBuffer commandBuffer) {
  vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
}

void Model::bind(EngineContext &context, VkCommandBuffer commandBuffer,
                 VkPipelineLayout pipelineLayout) {
  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

  vkCmdBindIndexBuffer(commandBuffer, *indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  VkDescriptorSet descriptorSets[2] = {context.frameInfo.globalDescriptorSet,
                                       textureDescriptorSet};
  vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                          pipelineLayout, 0, 2, descriptorSets, 0, nullptr);
}

std::vector<VkVertexInputBindingDescription>
Model::Vertex::getBindingDescriptions() {
  std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
  bindingDescriptions[0].binding = 0;
  bindingDescriptions[0].stride = sizeof(Vertex);
  bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
  return bindingDescriptions;
}

std::vector<VkVertexInputAttributeDescription>
Model::Vertex::getAttributeDescriptions() {
  std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

  attributeDescriptions.push_back(
      {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)});
  attributeDescriptions.push_back(
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
  attributeDescriptions.push_back(
      {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});

  return attributeDescriptions;
}
void Model::loadModel(const std::filesystem::path &path) {
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

  std::vector<Vertex> vertices;
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
              Vertex newVertex;
              newVertex.position = v;
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
    throw std::runtime_error(fmt::format("no image in GLB {}", path.string()));
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
                      texture = std::make_unique<Image>(
                          context,
                          reinterpret_cast<const void *>(vector.bytes.data() +
                                                         bufferView.byteOffset),
                          static_cast<int>(bufferView.byteLength));
                    }},
                buffer.data);
          },
      },
      image.data);

  createBuffers(vertices, indices);
}
void Model::createDescriptors() {
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = texture->getImageView();
  imageInfo.sampler = context.vulkan.modelSampler;
  DescriptorWriter(*context.vulkan.modelDescriptorSetLayout,
                   *context.vulkan.globalDescriptorPool)
      .writeImage(0, &imageInfo)
      .build(textureDescriptorSet);
}
Model::Model(EngineContext &context, const ModelCreateInfo &createInfo)
    : context{context} {
  createInfo.filepath.empty()
      ? createBuffers(createInfo.vertices, createInfo.indices)
      : loadModel(createInfo.filepath);

  createDescriptors();
}
} // namespace vkh
