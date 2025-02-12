#include "water.hpp"
#include <glm/ext/scalar_constants.hpp>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <GLFW/glfw3.h>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <memory>
#include <stdexcept>

#include "../buffer.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../entity.hpp"
#include "../image.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace waterSys {
std::unique_ptr<GraphicsPipeline> graphicsPipeline;
VkPipelineLayout graphicsPipelineLayout;
std::unique_ptr<DescriptorSetLayout> graphicsDescriptorSetLayout;
VkDescriptorSet graphicsDescriptorSet;

std::unique_ptr<Pipeline> computePipeline;
VkPipelineLayout computePipelineLayout;
std::unique_ptr<DescriptorSetLayout> computeDescriptorSetLayout;
VkDescriptorSet computeDescriptorSet;

struct GraphicsPushConstantData {
  glm::mat4 modelMatrix{1.f};
  float time;
};

struct ComputePushConstantData {
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

std::unique_ptr<Image> frequencyImage;
std::unique_ptr<Image> heightFieldImage;

std::vector<Vertex> vertices;

std::vector<uint32_t> indices;

void createVertexBuffer(EngineContext &context) {
  uint32_t count = static_cast<uint32_t>(vertices.size());
  VkDeviceSize bufSize = sizeof(vertices[0]) * count;
  uint32_t size = sizeof(vertices[0]);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = size;
  bufInfo.instanceCount = count;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(vertices.data());
  stagingBuffer.unmap();

  bufInfo.usage =
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);

  copyBuffer(context, stagingBuffer, *vertexBuffer, bufSize);
}
void createIndexBuffer(EngineContext &context) {
  uint32_t count = indices.size();
  VkDeviceSize bufSize = sizeof(indices[0]) * count;
  uint32_t size = sizeof(indices[0]);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = size;
  bufInfo.instanceCount = count;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(indices.data());
  stagingBuffer.unmap();

  bufInfo.usage =
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);

  copyBuffer(context, stagingBuffer, *indexBuffer, bufSize);
}
void createDescriptors(EngineContext &context) {
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  imageInfo.sampler = VK_NULL_HANDLE;
  imageInfo.imageView = frequencyImage->getImageView();
  computeDescriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                      VK_SHADER_STAGE_COMPUTE_BIT)
          .build();
  DescriptorWriter(*computeDescriptorSetLayout, *context.vulkan.globalPool)
      .writeImage(0, &imageInfo)
      .build(computeDescriptorSet);

  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = heightFieldImage->getImageView();
  imageInfo.sampler = context.vulkan.fontSampler;
  graphicsDescriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();
  DescriptorWriter(*graphicsDescriptorSetLayout, *context.vulkan.globalPool)
      .writeImage(0, &imageInfo)
      .build(graphicsDescriptorSet);
}

void createPipelineLayout(EngineContext &context) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(GraphicsPushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      context.vulkan.globalDescriptorSetLayout->getDescriptorSetLayout(),
      graphicsDescriptorSetLayout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &graphicsPipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");

  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushConstantRange.size = sizeof(ComputePushConstantData);

  descriptorSetLayouts.clear();
  descriptorSetLayouts = {computeDescriptorSetLayout->getDescriptorSetLayout()};
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &computePipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create compute pipeline layout!");
}
void createPipeline(EngineContext &context) {
  PipelineCreateInfo pipelineConfig{};
  pipelineConfig.pipelineLayout = graphicsPipelineLayout;
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.attributeDescriptions = attributeDescriptions;
  pipelineConfig.bindingDescriptions = bindingDescriptions;
  graphicsPipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/water.vert.spv", "shaders/water.frag.spv",
      pipelineConfig);

  computePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/waterfreq.comp.spv", computePipelineLayout);
}
constexpr int N = 512;
const float GRID_SCALE = 10.f;
void generateGrid() {
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      Vertex v;
      v.position.x = ((i / (N - 1.0f)) * 2.0f - 1.0f) * GRID_SCALE;
      v.position.z = ((j / (N - 1.0f)) * 2.0f - 1.0f) * GRID_SCALE;
      v.position.y = GROUND_LEVEL + 10.f;
      v.uv = glm::vec2(i / (N - 1.0f), j / (N - 1.0f));
      vertices.push_back(v);
    }
  }

  for (int i = 0; i < N - 1; ++i) {
    for (int j = 0; j < N - 1; ++j) {
      uint32_t topLeft = i * N + j;
      uint32_t topRight = topLeft + 1;
      uint32_t bottomLeft = (i + 1) * N + j;
      uint32_t bottomRight = bottomLeft + 1;

      indices.insert(indices.end(), {topLeft, bottomLeft, topRight});
      indices.insert(indices.end(), {topRight, bottomLeft, bottomRight});
    }
  }
}

void createImages(EngineContext &context) {
  ImageCreateInfo imageInfo{};
  imageInfo.w = imageInfo.h = N;

  imageInfo.format = VK_FORMAT_R32G32_SFLOAT;
  imageInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
  imageInfo.usage = VK_IMAGE_USAGE_STORAGE_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_SAMPLED_BIT;
  frequencyImage = std::make_unique<Image>(context, imageInfo);

  imageInfo.format = VK_FORMAT_R32_SFLOAT;
  imageInfo.usage = VK_IMAGE_USAGE_SAMPLED_BIT |
                    VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                    VK_IMAGE_USAGE_STORAGE_BIT;
  imageInfo.layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  std::vector<float> fillData(N * N);
  imageInfo.data = fillData.data();
  heightFieldImage = std::make_unique<Image>(context, imageInfo);
}
void init(EngineContext &context) {
  generateGrid();
  createVertexBuffer(context);
  createIndexBuffer(context);

  createImages(context);

  createDescriptors(context);
  createPipelineLayout(context);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  vertexBuffer = nullptr;
  indexBuffer = nullptr;
  graphicsPipeline = nullptr;
  frequencyImage = nullptr;
  heightFieldImage = nullptr;
  graphicsDescriptorSetLayout = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, graphicsPipelineLayout,
                          nullptr);

  computePipeline = nullptr;
  computeDescriptorSetLayout = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, computePipelineLayout,
                          nullptr);
}

void dispatchCompute(EngineContext &context) {
  computePipeline->bind(context.frameInfo.commandBuffer);
}
void render(EngineContext &context) {
  VkCommandBuffer computeCommandBuffer = beginSingleTimeCommands(context);
  computePipeline->bind(computeCommandBuffer);
  ComputePushConstantData computePush{};
  computePush.time = glfwGetTime();
  vkCmdPushConstants(computeCommandBuffer, computePipelineLayout,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(ComputePushConstantData), &computePush);
  vkCmdBindDescriptorSets(
      computeCommandBuffer, VK_PIPELINE_BIND_POINT_COMPUTE,
      computePipelineLayout, 0, 1, &computeDescriptorSet, 0, nullptr);
  vkCmdDispatch(computeCommandBuffer,
                N / 16, // Workgroups in X (local size is 16x16)
                N / 16, // Workgroups in Y
                1);
  endSingleTimeCommands(context, computeCommandBuffer, context.vulkan.computeQueue);

  graphicsPipeline->bind(context.frameInfo.commandBuffer);

  GraphicsPushConstantData graphicsPush{};
  Transform transform{.position = {5.f, GROUND_LEVEL, 0.f}};
  graphicsPush.modelMatrix = transform.mat4();
  graphicsPush.time = glfwGetTime();

  vkCmdPushConstants(context.frameInfo.commandBuffer, graphicsPipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0,
                     sizeof(GraphicsPushConstantData), &graphicsPush);

  // model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  // model->draw(context.frameInfo.commandBuffer);
  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.commandBuffer, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  VkDescriptorSet descriptorSets[2] = {context.frameInfo.globalDescriptorSet,
                                       graphicsDescriptorSet};
  vkCmdBindDescriptorSets(
      context.frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
      graphicsPipelineLayout, 0, 2, descriptorSets, 0, nullptr);

  vkCmdDrawIndexed(context.frameInfo.commandBuffer, indices.size(), 1, 0, 0, 0);
}
} // namespace waterSys
} // namespace vkh
