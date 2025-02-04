#include "model.hpp"
#include "fastgltf/tools.hpp"
#include <memory>
#include <stdexcept>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <fastgltf/core.hpp>
#include <fastgltf/glm_element_traits.hpp>
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
void Model::createVertexBuffer(const std::vector<Vertex> &vertices) {
  vertexCount = static_cast<uint32_t>(vertices.size());
  assert(vertexCount >= 3 && "Vertex count must be at least 3");
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
  uint32_t vertexSize = sizeof(vertices[0]);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = vertexSize;
  bufInfo.instanceCount = vertexCount;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer{
      context,
      bufInfo,
  };

  stagingBuffer.map();
  stagingBuffer.write((void *)vertices.data());

  bufInfo.usage =
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);

  copyBuffer(context, stagingBuffer, *vertexBuffer, bufferSize);
}

void Model::createIndexBuffer(const std::vector<uint32_t> &indices) {
  indexCount = static_cast<uint32_t>(indices.size());
  hasIndexBuffer = indexCount > 0;

  if (!hasIndexBuffer)
    return;

  VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
  uint32_t indexSize = sizeof(indices[0]);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = indexSize;
  bufInfo.instanceCount = indexCount;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer{
      context,
      bufInfo,
  };

  stagingBuffer.map();
  stagingBuffer.write((void *)indices.data());

  BufferCreateInfo bufInfo2;
  bufInfo2.instanceSize = indexSize;
  bufInfo2.instanceCount = indexCount;
  bufInfo.usage =
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);

  copyBuffer(context, stagingBuffer, *indexBuffer, bufferSize);
}

void Model::draw(VkCommandBuffer commandBuffer) {
  if (hasIndexBuffer)
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
  else
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}

void Model::bind(EngineContext &context, VkCommandBuffer commandBuffer,
                 VkPipelineLayout pipelineLayout) {
  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

  if (hasIndexBuffer) {
    vkCmdBindIndexBuffer(commandBuffer, *indexBuffer, 0, VK_INDEX_TYPE_UINT32);
  }
  if (enableTexture) {
    VkDescriptorSet descriptorSets[2] = {context.frameInfo.globalDescriptorSet,
                                         textureDescriptorSet};
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 2, descriptorSets, 0, nullptr);
  } else {
    vkCmdBindDescriptorSets(commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            pipelineLayout, 0, 1,
                            &context.frameInfo.globalDescriptorSet, 0, nullptr);
  }
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
  auto &mesh = gltf.meshes[0];

  for (auto &&p : mesh.primitives) {
    size_t initial_vtx = vertices.size();
    // load indexes
    {
      fastgltf::Accessor &indexaccessor =
          gltf.accessors[p.indicesAccessor.value()];
      indices.reserve(indices.size() + indexaccessor.count);

      fastgltf::iterateAccessor<std::uint32_t>(
          gltf, indexaccessor,
          [&](std::uint32_t idx) { indices.push_back(idx + initial_vtx); });
    }

    // load vertex positions
    {
      fastgltf::Accessor &posAccessor =
          gltf.accessors[p.findAttribute("POSITION")->accessorIndex];
      vertices.resize(vertices.size() + posAccessor.count);

      fastgltf::iterateAccessorWithIndex<glm::vec3>(
          gltf, posAccessor, [&](glm::vec3 v, size_t index) {
            Vertex newvtx;
            newvtx.position = v;
            vertices[initial_vtx + index] = newvtx;
          });
    }

    // load vertex normals
    auto normals = p.findAttribute("NORMAL");
    if (normals != p.attributes.end()) {

      fastgltf::iterateAccessorWithIndex<glm::vec3>(
          gltf, gltf.accessors[(*normals).accessorIndex],
          [&](glm::vec3 v, size_t index) {
            vertices[initial_vtx + index].normal = v;
          });
    }

    // load UVs
    auto uv = p.findAttribute("TEXCOORD_0");
    if (uv != p.attributes.end()) {

      fastgltf::iterateAccessorWithIndex<glm::vec2>(
          gltf, gltf.accessors[(*uv).accessorIndex],
          [&](glm::vec2 v, size_t index) {
            vertices[initial_vtx + index].uv.x = v.x;
            vertices[initial_vtx + index].uv.y = v.y;
          });
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

  createIndexBuffer(indices);
  createVertexBuffer(vertices);
}
void Model::createDescriptors() {
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = texture->getImageView();
  imageInfo.sampler = context.vulkan.modelSampler;
  DescriptorWriter(*context.vulkan.modelDescriptorSetLayout,
                   *context.vulkan.globalPool)
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
