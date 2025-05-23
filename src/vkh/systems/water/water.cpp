#include "water.hpp"

#include <memory>
#include <vulkan/vulkan_core.h>

#include "../../audio.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"
#include "WSTessendorf.hpp"

namespace vkh {
WaterSys::~WaterSys() {
  vkDestroySampler(context.vulkan.device, sampler, nullptr);
}
void WaterSys::createSampler() {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(context.vulkan.physicalDevice, &properties);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(context.vulkan.device, &samplerInfo, nullptr, &sampler) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
}
WaterSys::WaterSys(EngineContext &context)
    : System(context), modelTess(context),
      oceanSound("sounds/76007__noisecollector__capemay_delawarebay_loop.wav") {
  createSampler();

  createDescriptorSetLayout();

  createPipeline();

  createStagingBuffer();

  createMesh();

  createRenderData();
  prepare();

  // oceanSound.play(false, true);
}
void WaterSys::createPipeline() {
  PipelineCreateInfo pipelineInfo{};
  const VkDescriptorSetLayout dsl = *descriptorSetLayout;

  pipelineInfo.layoutInfo.setLayoutCount = 1;
  pipelineInfo.layoutInfo.pSetLayouts = &dsl;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::s_AttribDescriptions;
  pipelineInfo.bindingDescriptions = Vertex::s_BindingDescriptions;
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/water/water.vert.spv", "shaders/water/water.frag.spv",
      pipelineInfo);
}
void WaterSys::createUniformBuffers(const uint32_t bufferCount) {
  const VkDeviceSize bufferSize =
      getUniformBufferAlignment(context, sizeof(VertexUBO)) +
      getUniformBufferAlignment(context, sizeof(WaterSurfaceUBO));

  uniformBuffers.resize(bufferCount);
  for (auto &buffer : uniformBuffers) {
    buffer = std::make_unique<Buffer<std::byte>>(
        context, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, bufferSize);
    buffer->map();
  }
}
void WaterSys::createDescriptorSets(const uint32_t count) {
  descriptorSets.resize(count);
  for (auto &set : descriptorSets) {
    context.vulkan.globalDescriptorPool->allocateDescriptorSet(
        *descriptorSetLayout, set);
  }
}

void WaterSys::createRenderData() {
  uint32_t imageCount = context.vulkan.swapChain->imageCount();
  createUniformBuffers(imageCount);
  createDescriptorSets(imageCount);
}

std::unique_ptr<Image> createMap(EngineContext &context,
                                 VkCommandBuffer cmdBuffer, const uint32_t size,
                                 const VkFormat mapFormat) {
  return std::make_unique<Image>(context, size, size, mapFormat,
                                 VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                     VK_IMAGE_USAGE_SAMPLED_BIT,
                                 VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
}
void WaterSys::createFrameMaps(VkCommandBuffer cmdBuffer) {
  vkQueueWaitIdle(context.vulkan.graphicsQueue);

  const uint32_t kSize = maxTileSize;

  frameMap.displacementMap =
      createMap(context, cmdBuffer, kSize, mapFormat);
  frameMap.normalMap =
      createMap(context, cmdBuffer, kSize, mapFormat);
}
void WaterSys::updateFrameMaps(VkCommandBuffer cmd, FrameMapData &frame) {
  VkDeviceSize stagingBufferOffset =
      alignSizeTo(mesh->getVerticesSize() + mesh->getIndicesSize(),
                  Image::formatSize(mapFormat));

  frame.displacementMap->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  frame.displacementMap->recordCopyFromBuffer(
      cmd, *stagingBuffer, static_cast<uint32_t>(stagingBufferOffset));
  frame.displacementMap->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  const VkDeviceSize mapSize = Image::formatSize(mapFormat) *
                               frame.displacementMap->w *
                               frame.displacementMap->h;
  stagingBufferOffset += mapSize;

  frame.normalMap->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  frame.normalMap->recordCopyFromBuffer(
      cmd, *stagingBuffer, static_cast<uint32_t>(stagingBufferOffset));
  frame.normalMap->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}
void WaterSys::copyModelTessDataToStagingBuffer() {
  assert(stagingBuffer != nullptr);

  unsigned int tileSizeSquared =
      modelTess.getTileSize() * modelTess.getTileSize();
  const VkDeviceSize displacementsSize =
      sizeof(WSTessendorf::Displacement) * tileSizeSquared;
  const VkDeviceSize normalsSize =
      sizeof(WSTessendorf::Normal) * tileSizeSquared;

  // StagingBuffer layout:
  //  ----------------------------------------------
  // | Vertices | Indices | Displacements | Normals |
  //  ----------------------------------------------
  // mapped

  VkDeviceSize offset;

  auto cmd = beginSingleTimeCommands(context);
  offset = alignSizeTo(mesh->getVerticesSize() + mesh->getIndicesSize(),
                       Image::formatSize(mapFormat));
  stagingBuffer->recordCopyFromBuffer(cmd, modelTess.getDisplacementsBuffer(),
                                      displacementsSize, 0, offset);

  offset += displacementsSize;

  stagingBuffer->recordCopyFromBuffer(cmd, modelTess.getNormalsBuffer(),
                                      normalsSize, 0, offset);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  stagingBuffer->flush(getNonCoherentAtomSizeAlignment(
      context, alignSizeTo(mesh->getVerticesSize() + mesh->getIndicesSize(),
                           Image::formatSize(mapFormat)) +
                   displacementsSize + normalsSize));
}
std::vector<WaterSys::Vertex>
WaterSys::createGridVertices(const uint32_t tileSize, const float scale) {
  std::vector<Vertex> vertices;

  vertices.reserve(getTotalVertexCount(tileSize));

  const int32_t halfSize = tileSize / 2;

  for (int32_t y = -halfSize; y <= halfSize; ++y) {
    for (int32_t x = -halfSize; x <= halfSize; ++x) {
      vertices.emplace_back(
          // Position
          glm::vec3(static_cast<float>(x), // x
                    0.0f,                  // y
                    static_cast<float>(y)  // z
                    ) *
              scale,
          // Texcoords
          glm::vec2(static_cast<float>(x + halfSize), // u
                    static_cast<float>(y + halfSize)  // v
                    ) /
              static_cast<float>(tileSize));
    }
  }

  return vertices;
}

std::vector<uint32_t> WaterSys::createGridIndices(const uint32_t tileSize) {
  const uint32_t vertexCount = tileSize + 1;

  std::vector<uint32_t> indices;
  indices.reserve(getTotalIndexCount(vertexCount * vertexCount));

  for (uint32_t y = 0; y < tileSize; ++y) {
    for (uint32_t x = 0; x < tileSize; ++x) {
      const uint32_t kVertexIndex = y * vertexCount + x;

      // Top triangle
      indices.emplace_back(kVertexIndex);
      indices.emplace_back(kVertexIndex + vertexCount);
      indices.emplace_back(kVertexIndex + 1);

      // Bottom triangle
      indices.emplace_back(kVertexIndex + 1);
      indices.emplace_back(kVertexIndex + vertexCount);
      indices.emplace_back(kVertexIndex + vertexCount + 1);
    }
  }

  return indices;
}
void WaterSys::updateDescriptorSet(VkDescriptorSet set) {
  // UBOs
  VkDescriptorBufferInfo bufferInfos[2] = {};
  bufferInfos[0].buffer = *uniformBuffers[context.frameInfo.frameIndex];
  bufferInfos[0].offset = 0;
  bufferInfos[0].range = sizeof(VertexUBO);

  bufferInfos[1].buffer = *uniformBuffers[context.frameInfo.frameIndex];
  bufferInfos[1].offset = getUniformBufferAlignment(context, sizeof(VertexUBO));
  bufferInfos[1].range = sizeof(WaterSurfaceUBO);

  // textures
  assert(frameMap.displacementMap != nullptr);
  assert(frameMap.normalMap != nullptr);

  VkDescriptorImageInfo imageInfos[2] = {};
  imageInfos[0] = frameMap.displacementMap->getDescriptorInfo(sampler);
  // TODO force future image layout
  imageInfos[0].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfos[1] = frameMap.normalMap->getDescriptorInfo(sampler);
  // TODO force future image layout
  imageInfos[1].imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

  DescriptorWriter descriptorWriter(*descriptorSetLayout,
                                    *context.vulkan.globalDescriptorPool);
  uint32_t binding = 0;

  descriptorWriter.writeBuffer(binding++, &bufferInfos[0])
      .writeBuffer(binding++, &bufferInfos[1])
      .writeImage(binding++, &imageInfos[0])
      .writeImage(binding++, &imageInfos[1]);

  descriptorWriter.overwrite(set);
}
void WaterSys::updateUniformBuffer() {
  auto &buffer = uniformBuffers[context.frameInfo.frameIndex];

  buffer->write(&vertexUBO, sizeof(vertexUBO));
  buffer->write(&waterSurfaceUBO, sizeof(waterSurfaceUBO),

                getUniformBufferAlignment(context, sizeof(VertexUBO)));

  buffer->flush();
}
void WaterSys::prepare() {
  auto cmd = beginSingleTimeCommands(context);
  createFrameMaps(cmd);

  // Do one pass to initialize the maps
  vertexUBO.WSHeightAmp =
      modelTess.computeWaves(static_cast<float>(glfwGetTime()) * animSpeed);
  copyModelTessDataToStagingBuffer();

  updateFrameMaps(cmd, frameMap);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void WaterSys::update(const SkyParams &skyParams) {
  vertexUBO.model = glm::mat4(1.0f);
  vertexUBO.view = context.camera.viewMatrix;
  vertexUBO.proj = context.camera.projectionMatrix;

  vertexUBO.WSChoppy = modelTess.getDisplacementLambda();

  waterSurfaceUBO.camPos = context.camera.position;
  waterSurfaceUBO.sky = skyParams;

  updateUniformBuffer();
  // UpdateMeshBuffers(device, cmdBuffer);
  updateDescriptorSet(descriptorSets[context.frameInfo.frameIndex]);
  if (playAnimation) {
    updateFrameMaps(context.frameInfo.commandBuffer, frameMap);
    // auto cmd = beginSingleTimeCommands(context);
    // vkCmdDispatch(cmd, 16, 16, 1);
    // endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);
    vertexUBO.WSHeightAmp =
        modelTess.computeWaves(static_cast<float>(glfwGetTime()) * animSpeed);
    copyModelTessDataToStagingBuffer();
  }
}

void WaterSys::render() {
  pipeline->bind(context.frameInfo.commandBuffer);

  const uint32_t kFirstSet = 0, kDescriptorSetCount = 1;
  const uint32_t kDynamicOffsetCount = 0;
  const uint32_t *kDynamicOffsets = nullptr;

  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, kFirstSet,
                          kDescriptorSetCount,
                          &descriptorSets[context.frameInfo.frameIndex],
                          kDynamicOffsetCount, kDynamicOffsets);

  mesh->bind(context, context.frameInfo.commandBuffer, *pipeline);
  mesh->begin()->draw(context.frameInfo.commandBuffer);
}

void WaterSys::createDescriptorSetLayout() {
  uint32_t bindingPoint = 0;

  descriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          // VertexUBO
          .addBinding(bindingPoint++, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      VK_SHADER_STAGE_VERTEX_BIT)
          // WaterSurfaceUBO
          .addBinding(bindingPoint++, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      VK_SHADER_STAGE_FRAGMENT_BIT)
          // Displacement map
          .addBinding(bindingPoint++, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT)
          // Normal map
          .addBinding(bindingPoint++, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT)
          .build();
}

void WaterSys::createMesh() {
  const VkDeviceSize kMaxVerticesSize = sizeof(Vertex) * getMaxVertexCount();
  const VkDeviceSize kMaxIndicesSize = sizeof(uint32_t) * getMaxIndexCount();

  std::vector<Vertex> vertices =
      createGridVertices(m_TileSize, m_VertexDistance);
  std::vector<uint32_t> indices = createGridIndices(m_TileSize);

  MeshCreateInfo<Vertex> meshInfo{.vertices = vertices, .indices = indices};
  mesh = std::make_unique<Scene<Vertex>>(context, meshInfo);
}

void WaterSys::createStagingBuffer() {
  const VkDeviceSize kVerticesSize =
      sizeof(Vertex) * getTotalVertexCount(m_TileSize);
  const VkDeviceSize indicesSize =
      sizeof(uint32_t) * getTotalIndexCount(getTotalVertexCount(m_TileSize));
  const VkDeviceSize kMapSize =
      Image::formatSize(mapFormat) * m_TileSize * m_TileSize;
  const VkDeviceSize totalSize =
      alignSizeTo(kVerticesSize + indicesSize, Image::formatSize(mapFormat)) +
      (kMapSize * 2);

  stagingBuffer = std::make_unique<Buffer<std::byte>>(
      context,
      VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, totalSize);
  stagingBuffer->map();
}
} // namespace vkh
