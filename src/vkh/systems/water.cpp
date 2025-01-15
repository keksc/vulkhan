#include "water.hpp"
#include <GLFW/glfw3.h>
#include <algorithm>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>

#include <complex>
#include <memory>
#include <stdexcept>

#include <cassert>

#include "../buffer.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../entity.hpp"
#include "../image.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace waterSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;
std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
VkDescriptorSet descriptorSet;

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  float time;
};

struct Vertex {
  glm::vec3 position{};
  glm::vec2 uv{};
};

std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}};
std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
    {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};

std::unique_ptr<Buffer> vertexBuffer;
std::unique_ptr<Buffer> indexBuffer;

std::unique_ptr<Image> heightFieldImage;

void generateGridMesh(int width, int height, std::vector<Vertex> &vertices,
                      std::vector<uint32_t> &indices) {
  for (int y = 0; y <= height; ++y) {
    for (int x = 0; x <= width; ++x) {
      // Reverse the Y-coordinate
      float u = static_cast<float>(x) / width;
      float v = static_cast<float>(height - y) / height;
      vertices.push_back({glm::vec3(u, 0.0f, v), glm::vec2(u, v)});
    }
  }

  // Generate indices
  for (int y = 0; y < height; ++y) {
    for (int x = 0; x < width; ++x) {
      uint32_t topLeft = y * (width + 1) + x;
      uint32_t topRight = y * (width + 1) + x + 1;
      uint32_t bottomLeft = (y + 1) * (width + 1) + x;
      uint32_t bottomRight = (y + 1) * (width + 1) + x + 1;

      indices.push_back(topLeft);
      indices.push_back(bottomLeft);
      indices.push_back(topRight);

      indices.push_back(topRight);
      indices.push_back(bottomLeft);
      indices.push_back(bottomRight);
    }
  }
}

std::vector<Vertex> vertices; /* = {{{-1.f, GROUND_LEVEL, -1.f}, {0.f, 0.f}},
                                 {{1.f, GROUND_LEVEL, -1.f}, {1.f, 0.f}},
                                 {{-1.f, GROUND_LEVEL, 1.f}, {0.f, 1.f}},
                                 {{1.f, GROUND_LEVEL, 1.f}, {1.f, 1.f}}};*/

std::vector<uint32_t> indices; // = {0, 2, 1, 1, 2, 3};

void createVertexBuffer(EngineContext &context) {
  uint32_t vertexCount = static_cast<uint32_t>(vertices.size());
  assert(vertexCount >= 3 && "Vertex count must be at least 3");
  VkDeviceSize bufferSize = sizeof(vertices[0]) * vertexCount;
  uint32_t vertexSize = sizeof(vertices[0]);

  Buffer stagingBuffer{
      context,
      "water vertex buffer staging",
      vertexSize,
      vertexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)vertices.data());

  vertexBuffer = std::make_unique<Buffer>(
      context, "water vertex buffer", vertexSize, vertexCount,
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  copyBuffer(context, stagingBuffer.getBuffer(), vertexBuffer->getBuffer(),
             bufferSize);
}
void createIndexBuffer(EngineContext &context) {
  uint32_t indexCount = indices.size();
  VkDeviceSize bufferSize = sizeof(indices[0]) * indexCount;
  uint32_t indexSize = sizeof(indices[0]);

  Buffer stagingBuffer{
      context,
      "water index buffer staging",
      indexSize,
      indexCount,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
  };

  stagingBuffer.map();
  stagingBuffer.writeToBuffer((void *)indices.data());

  indexBuffer = std::make_unique<Buffer>(
      context, "water index buffer", indexSize, indexCount,
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  copyBuffer(context, stagingBuffer.getBuffer(), indexBuffer->getBuffer(),
             bufferSize);
}
void createDescriptors(EngineContext &context) {
  descriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = heightFieldImage->getImageView();
  imageInfo.sampler = context.vulkan.fontSampler;
  DescriptorWriter(*descriptorSetLayout, *context.vulkan.globalPool)
      .writeImage(0, &imageInfo)
      .build(descriptorSet);
}

void createPipelineLayout(EngineContext &context) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      context.vulkan.globalDescriptorSetLayout->getDescriptorSetLayout(),
      descriptorSetLayout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &pipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
}
void createPipeline(EngineContext &context) {
  assert(pipelineLayout != nullptr &&
         "Cannot create pipeline before pipeline layout");

  PipelineConfigInfo pipelineConfig{};
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.attributeDescriptions = attributeDescriptions;
  pipelineConfig.bindingDescriptions = bindingDescriptions;
  pipeline = std::make_unique<Pipeline>(
      context, "water system", "shaders/water.vert.spv",
      "shaders/water.frag.spv", pipelineConfig);
}
void init(EngineContext &context) {
  int N = 256;

  generateGridMesh(N, N, vertices, indices);
  createVertexBuffer(context);
  createIndexBuffer(context);

  std::vector<float> heightData(N * N);

  heightFieldImage =
      std::make_unique<Image>(context, "height field image", N, N,
                              heightData.data(), VK_FORMAT_R32_SFLOAT);

  createDescriptors(context);

  createPipelineLayout(context);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  vertexBuffer = nullptr;
  indexBuffer = nullptr;
  pipeline = nullptr;
  heightFieldImage = nullptr;
  descriptorSetLayout = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  PushConstantData push{};
  Transform transform{.position = {5.f, GROUND_LEVEL, 0.f}};
  push.modelMatrix = transform.mat4();
  push.time = glfwGetTime();

  vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData),
                     &push);

  // model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  // model->draw(context.frameInfo.commandBuffer);
  VkBuffer buffers[] = {vertexBuffer->getBuffer()};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.commandBuffer,
                       indexBuffer->getBuffer(), 0, VK_INDEX_TYPE_UINT32);
  VkDescriptorSet descriptorSets[2] = {context.frameInfo.globalDescriptorSet,
                                       descriptorSet};
  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 2,
                          descriptorSets, 0, nullptr);

  vkCmdDrawIndexed(context.frameInfo.commandBuffer, indices.size(), 1, 0, 0, 0);
}
} // namespace waterSys
} // namespace vkh
