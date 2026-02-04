#include "WSTessendorf.hpp"

#include <vulkan/vulkan_core.h>

#include <format>
#include <limits>
#include <memory>
#include <stdexcept>

#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"

namespace vkh {
void WSTessendorf::createDescriptors() {
  std::vector<VkDescriptorSetLayoutBinding> bindings = {
      VkDescriptorSetLayoutBinding{
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      VkDescriptorSetLayoutBinding{
          .binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      VkDescriptorSetLayoutBinding{
          .binding = 2,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      }};
  preFFTSetLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(preFFTSetLayout),
                    "WaterSys pre FFT descriptor set layout");
  preFFTSet =
      context.vulkan.globalDescriptorAllocator->allocate(preFFTSetLayout);
  DescriptorWriter writer(context);
  writer.writeBuffer(0, FFTData->descriptorInfo(),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, baseWaveHeightField->descriptorInfo(),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, waveVectors->descriptorInfo(),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(preFFTSet);

  std::vector<VkDescriptorSetLayoutBinding> postBindings = {
      VkDescriptorSetLayoutBinding{
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
      VkDescriptorSetLayoutBinding{
          .binding = 1,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      }};

  postFFTSetLayout = buildDescriptorSetLayout(context, postBindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(postFFTSetLayout),
                    "WaterSys post FFT descriptor set layout");
  postFFTSet =
      context.vulkan.globalDescriptorAllocator->allocate(postFFTSetLayout);
  writer.clear();
  writer.writeBuffer(0, FFTData->descriptorInfo(),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeImage(
      1, displacementFoamMap->getDescriptorInfo(context.vulkan.defaultSampler),
      VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
  writer.updateSet(postFFTSet);
}

WSTessendorf::WSTessendorf(EngineContext &context) : System(context) {
  VkFenceCreateInfo fenceCreateInfo{};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  vkCreateFence(context.vulkan.device, &fenceCreateInfo, nullptr, &fence);

  FFTData = std::make_unique<Buffer<std::complex<float>>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 3 * tileSizeSquared);

  baseWaveHeightField = std::make_unique<Buffer<BaseWaveHeight>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tileSizeSquared);

  waveVectors = std::make_unique<Buffer<WaveVector>>(
      context,
      VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, tileSizeSquared);

  displacementFoamMap = std::make_unique<Image>(
      context, glm::uvec2{tileSize}, VK_FORMAT_R16G16B16A16_SFLOAT,
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT |
          VK_IMAGE_USAGE_STORAGE_BIT,
      VK_IMAGE_LAYOUT_GENERAL, "water displacement map");

  VkFFTConfiguration config{};
  config.physicalDevice = &context.vulkan.physicalDevice;
  config.device = &context.vulkan.device;
  config.queue = &context.vulkan.computeQueue;
  config.commandPool = &context.vulkan.commandPool;
  config.isCompilerInitialized = 0;
  config.fence = &fence;
  config.FFTdim = 2;
  config.size[0] = tileSize;
  config.size[1] = tileSize;
  config.doublePrecision = 0;
  config.halfPrecision = 0;
  config.numberBatches = 3;
  config.useLUT = 0;
  config.performR2C = 0;
  config.performDCT = 0;
  config.disableReorderFourStep = 0;

  uint64_t bufSize =
      3 * config.size[0] * config.size[1] * sizeof(std::complex<float>);
  config.buffer = *FFTData;
  config.bufferSize = &bufSize;
  config.bufferNum = 1;
  config.makeInversePlanOnly = 1;

  VkFFTResult result = initializeVkFFT(&app, config);
  if (result)
    throw std::runtime_error(
        std::format("couldnt init VkFFT, error {}", static_cast<int>(result)));

  createDescriptors();

  VkSpecializationMapEntry specMapEntry{};
  specMapEntry.size = sizeof(uint32_t);
  specMapEntry.constantID = 0;
  specMapEntry.offset = 0;

  VkSpecializationInfo specInfo{};
  specInfo.mapEntryCount = 1;
  specInfo.dataSize = sizeof(uint32_t);
  specInfo.pMapEntries = &specMapEntry;
  specInfo.pData = &tileSize;

  VkPipelineLayoutCreateInfo FFTLayoutInfo{};
  FFTLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  FFTLayoutInfo.setLayoutCount = 1;
  FFTLayoutInfo.pSetLayouts = &preFFTSetLayout;
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.size = sizeof(PushConstantData);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  FFTLayoutInfo.pushConstantRangeCount = 1;
  FFTLayoutInfo.pPushConstantRanges = &pushConstantRange;
  preFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/preFFT.comp.spv", FFTLayoutInfo,
      "water pre FFT pipeline", &specInfo);

  FFTLayoutInfo.pSetLayouts = &postFFTSetLayout;
  postFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/postFFT.comp.spv", FFTLayoutInfo,
      "water post FFT pipeline", &specInfo);

  VkPipelineLayoutCreateInfo baseWaveHeightFieldLayoutInfo{};
  baseWaveHeightFieldLayoutInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  baseWaveHeightFieldLayoutInfo.setLayoutCount = 1;
  baseWaveHeightFieldLayoutInfo.pSetLayouts = &preFFTSetLayout;
  ComputePipeline baseWaveHeightFieldPipeline(
      context, "shaders/water/baseWaveHeightField.comp.spv",
      baseWaveHeightFieldLayoutInfo, "water base wave height field pipeline",
      &specInfo);

  Buffer<WaveVector> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                       VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                   tileSizeSquared);

  std::vector<WaveVector> waveVecs;
  computeWaveVectors(waveVecs);
  stagingBuffer.map();
  stagingBuffer.write(waveVecs.data());
  stagingBuffer.unmap();

  auto cmd = beginSingleTimeCommands(context);
  waveVectors->recordCopyFromBuffer(cmd, stagingBuffer);
  baseWaveHeightFieldPipeline.bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          baseWaveHeightFieldPipeline, 0, 1, &preFFTSet, 0,
                          nullptr);
  vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);
  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);
}

WSTessendorf::~WSTessendorf() {
  deleteVkFFT(&app);
  vkDestroyFence(context.vulkan.device, fence, nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, preFFTSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, postFFTSetLayout,
                               nullptr);
}

void WSTessendorf::computeWaveVectors(std::vector<WaveVector> &waveVecs) {
  waveVecs.reserve(tileSizeSquared);

  for (int32_t m = 0; m < tileSize; ++m) {
    for (int32_t n = 0; n < tileSize; ++n) {
      waveVecs.emplace_back(
          glm::vec2(glm::pi<float>() * (2.0f * n - tileSize) / tileLength,
                    glm::pi<float>() * (2.0f * m - tileSize) / tileLength));
    }
  }
}

void WSTessendorf::recordComputeWaves(VkCommandBuffer &cmd, float t) {
  debug::beginLabel(context, cmd, "record compute waves", {.2f, .2f, 1.f, 1.f});
  {
    PushConstantData data{.t = t, .dt = context.frameInfo.dt};
    debug::beginLabel(context, cmd, "pre FFT", {.2f, .2f, 1.f, 1.f});
    {
      vkCmdPushConstants(cmd, preFFTPipeline->getLayout(),
                         VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(PushConstantData), &data);
      preFFTPipeline->bind(cmd);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              preFFTPipeline->getLayout(), 0, 1, &preFFTSet, 0,
                              nullptr);
      vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);
    }
    debug::endLabel(context, cmd);

    VkMemoryBarrier memoryBarrier{};
    memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr);

    debug::beginLabel(context, cmd, "FFT", {.2f, .2f, 1.f, 1.f});
    {
      VkFFTLaunchParams launchParams{};
      launchParams.buffer = *FFTData;
      launchParams.commandBuffer = &cmd;
      VkFFTResult result = VkFFTAppend(&app, 1, &launchParams);
    }
    debug::endLabel(context, cmd);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr);

    debug::beginLabel(context, cmd, "post FFT", {.2f, .2f, 1.f, 1.f});
    {
      displacementFoamMap->recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_GENERAL);

      vkCmdPushConstants(cmd, postFFTPipeline->getLayout(),
                         VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(PushConstantData), &data);
      postFFTPipeline->bind(cmd);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              postFFTPipeline->getLayout(), 0, 1, &postFFTSet,
                              0, nullptr);
      vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);

      displacementFoamMap->recordTransitionLayout(
          cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    }
    debug::endLabel(context, cmd);
  }
  debug::endLabel(context, cmd);
}
} // namespace vkh
