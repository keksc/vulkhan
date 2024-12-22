#include "model.hpp"

#include "utils.hpp"
#include <vulkan/vulkan_core.h>

#define TINYOBJLOADER_IMPLEMENTATION
#include <tiny_obj_loader.h>
#define GLM_ENABLE_EXPERIMENTAL
#include <fmt/color.h>
#include <glm/gtx/hash.hpp>

#include "descriptors.hpp"
#include "deviceHelpers.hpp"

#include <cassert>
#include <cstring>
#include <unordered_map>

namespace std {
template <> struct hash<vkh::Model::Vertex> {
  size_t operator()(vkh::Model::Vertex const &vertex) const {
    size_t seed = 0;
    vkh::hashCombine(seed, vertex.position, vertex.color, vertex.normal,
                     vertex.uv);
    return seed;
  }
};
} // namespace std

namespace vkh {

Model::~Model() {
  if (enableTexture) {
    vkDestroySampler(context.vulkan.device, textureSampler, nullptr);
    vkDestroyImageView(context.vulkan.device, textureImageView, nullptr);
    vkDestroyImage(context.vulkan.device, textureImage, nullptr);
    vkFreeMemory(context.vulkan.device, textureImageMemory, nullptr);
  }
  fmt::print("{} model {}\n",
             fmt::styled("destroyed", fmt::fg(fmt::color::red)),
             fmt::styled(name, fg(fmt::color::yellow)));
}

void Model::createVertexBuffer(const std::vector<Vertex> &vertices) {
  vertexCount = static_cast<uint32_t>(vertices.size());
  assert(vertexCount >= 3 && "Vertex count must be at least 3");
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
  uint32_t vertexSize = sizeof(vertices[0]);

  Buffer stagingBuffer{
      context,
      fmt::format("{} vertex buffer staging", name),
      vertexSize,
      vertexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)vertices.data());

  vertexBuffer = std::make_unique<Buffer>(
      context, fmt::format("{} vertex buffer", name), vertexSize, vertexCount,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  copyBuffer(context, stagingBuffer.getBuffer(), vertexBuffer->getBuffer(),
             bufferSize);
}

void Model::createIndexBuffer(const std::vector<uint32_t> &indices) {
  indexCount = static_cast<uint32_t>(indices.size());
  hasIndexBuffer = indexCount > 0;

  if (!hasIndexBuffer)
    return;

  VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
  uint32_t indexSize = sizeof(indices[0]);

  Buffer stagingBuffer{
      context,
      fmt::format("{} index buffer staging", name),
      indexSize,
      indexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)indices.data());

  indexBuffer = std::make_unique<Buffer>(
      context, fmt::format("{} index buffer", name), indexSize, indexCount,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  copyBuffer(context, stagingBuffer.getBuffer(), indexBuffer->getBuffer(),
             bufferSize);
}

void Model::draw(VkCommandBuffer commandBuffer) {
  if (hasIndexBuffer)
    vkCmdDrawIndexed(commandBuffer, indexCount, 1, 0, 0, 0);
  else
    vkCmdDraw(commandBuffer, vertexCount, 1, 0, 0);
}

void Model::bind(EngineContext &context, VkCommandBuffer commandBuffer,
                 VkPipelineLayout pipelineLayout) {
  VkBuffer buffers[] = {vertexBuffer->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(commandBuffer, 0, 1, buffers, offsets);

  if (hasIndexBuffer) {
    vkCmdBindIndexBuffer(commandBuffer, indexBuffer->getBuffer(), 0,
                         VK_INDEX_TYPE_UINT32);
  }
  if (enableTexture) {
    VkDescriptorSet descriptorSets[] = {context.frameInfo.globalDescriptorSet,
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
      {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color)});
  attributeDescriptions.push_back(
      {2, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)});
  attributeDescriptions.push_back(
      {3, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});

  return attributeDescriptions;
}

void Model::loadModel(const std::string &filepath) {
  tinyobj::attrib_t attrib;
  std::vector<tinyobj::shape_t> shapes;
  std::vector<tinyobj::material_t> materials;
  std::string warn, err;

  if (!tinyobj::LoadObj(&attrib, &shapes, &materials, &warn, &err,
                        filepath.c_str())) {
    throw std::runtime_error(warn + err);
  }

  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;

  std::unordered_map<Vertex, uint32_t> uniqueVertices{};
  for (const auto &shape : shapes) {
    for (const auto &index : shape.mesh.indices) {
      Vertex vertex{};

      if (index.vertex_index >= 0) {
        vertex.position = {
            attrib.vertices[3 * index.vertex_index + 0],
            attrib.vertices[3 * index.vertex_index + 1],
            attrib.vertices[3 * index.vertex_index + 2],
        };
        vertex.color = {
            attrib.colors[3 * index.vertex_index + 0],
            attrib.colors[3 * index.vertex_index + 1],
            attrib.colors[3 * index.vertex_index + 2],
        };
      }

      if (index.normal_index >= 0) {
        vertex.normal = {
            attrib.normals[3 * index.normal_index + 0],
            attrib.normals[3 * index.normal_index + 1],
            attrib.normals[3 * index.normal_index + 2],
        };
      }

      if (index.texcoord_index >= 0) {
        vertex.uv = {
            attrib.texcoords[2 * index.texcoord_index + 0],
            1.0 - attrib.texcoords[2 * index.texcoord_index + 1],
        };
      }

      if (uniqueVertices.count(vertex) == 0) {
        uniqueVertices[vertex] = static_cast<uint32_t>(vertices.size());
        vertices.push_back(vertex);
      }
      indices.push_back(uniqueVertices[vertex]);
    }
  }
  createIndexBuffer(indices);
  createVertexBuffer(vertices);
}
void Model::createDescriptors() {
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = textureImageView;
  imageInfo.sampler = textureSampler;
  DescriptorWriter(*context.vulkan.modelDescriptorSetLayout,
                   *context.vulkan.globalPool)
      .writeImage(0, &imageInfo)
      .build(textureDescriptorSet);
}
void Model::createDefaultBlackTextureImage() {
  uint32_t blackPixel = 0xff000000;
  VkDeviceSize imageSize = sizeof(blackPixel);
  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(context, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(context.vulkan.device, stagingBufferMemory, 0, imageSize, 0,
              &data);
  memcpy(data, &blackPixel, static_cast<size_t>(imageSize));
  vkUnmapMemory(context.vulkan.device, stagingBufferMemory);

  VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = VK_FORMAT_R8G8B8A8_SRGB,
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  imageInfo.extent.width = 1;
  imageInfo.extent.height = 1;
  imageInfo.extent.depth = 1;

  createImageWithInfo(context, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      textureImage, textureImageMemory);

  transitionImageLayout(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer, textureImage,
                    static_cast<uint32_t>(1), static_cast<uint32_t>(1));
  transitionImageLayout(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(context.vulkan.device, stagingBuffer, nullptr);
  vkFreeMemory(context.vulkan.device, stagingBufferMemory, nullptr);
}
Model::Model(EngineContext &context, const std::string &name,
             const std::string &filepath, bool enableTexture)
    : context{context}, name{name}, enableTexture{enableTexture} {
  loadModel(filepath);
  if (enableTexture) {
    createDefaultBlackTextureImage();
    textureImageView =
        createImageView(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB);
    textureSampler = createTextureSampler(context);
    createDescriptors();
    fmt::print("{} model {} with default black texture\n",
               fmt::styled("created", fmt::fg(fmt::color::light_green)),
               fmt::styled(name, fg(fmt::color::yellow)));
    setDebugObjNames();
  } else {
    fmt::print("{} model {} without a texture\n",
               fmt::styled("created", fmt::fg(fmt::color::light_green)),
               fmt::styled(name, fg(fmt::color::yellow)));
  }
}
Model::Model(EngineContext &context, const std::string &name,
             const std::string &filepath, const std::string &texturepath)
    : context{context}, name{name} {
  loadModel(filepath);
  textureImage = createTextureImage(context, textureImageMemory, texturepath);
  textureImageView =
      createImageView(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB);
  textureSampler = createTextureSampler(context);
  createDescriptors();
  fmt::print("{} model {} with texture {}\n",
             fmt::styled("created", fmt::fg(fmt::color::light_green)),
             fmt::styled(name, fg(fmt::color::yellow)),
             fmt::styled(texturepath, fg(fmt::color::azure)));
  setDebugObjNames();
}
} // namespace vkh
