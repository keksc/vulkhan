#include "WSTessendorf.hpp"

#include <vulkan/vulkan_core.h>

#include <algorithm>
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
  VkDescriptorBufferInfo preFFTDescriptorInfos[] = {
      FFTData->descriptorInfo(), baseWaveHeightField->descriptorInfo(),
      waveVectors->descriptorInfo()};
  preFFTSet =
      context.vulkan.globalDescriptorAllocator->allocate(preFFTSetLayout);
  DescriptorWriter writer(context);
  writer.writeBuffer(0, preFFTDescriptorInfos[0],
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, preFFTDescriptorInfos[1],
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, preFFTDescriptorInfos[2],
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(preFFTSet);

  postFFTSetLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(postFFTSetLayout),
                    "WaterSys post FFT descriptor set layout");
  VkDescriptorBufferInfo postFFTDescriptorInfos[] = {
      FFTData->descriptorInfo(), displacementsAndNormals->descriptorInfo(),
      displacementsAndNormals->descriptorInfo()};
  postFFTSet =
      context.vulkan.globalDescriptorAllocator->allocate(postFFTSetLayout);
  writer.clear();
  writer.writeBuffer(0, postFFTDescriptorInfos[0],
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, postFFTDescriptorInfos[1],
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, postFFTDescriptorInfos[2],
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(postFFTSet);

  // Reduction descriptor set layout for stage 0
  reductionSetLayout0 = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(reductionSetLayout0),
                    "WaterSys reduction descriptor set layout #0");

  // Reduction descriptor set layout for subsequent stages
  bindings.emplace_back(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                        VK_SHADER_STAGE_COMPUTE_BIT);
  reductionSetLayout1 = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(reductionSetLayout1),
                    "WaterSys reduction descriptor set layout #1");

  // Create reduction descriptor sets
  VkDescriptorBufferInfo reductionInfos0[] = {
      displacementsAndNormals->descriptorInfo(),
      minReductionBuffer0->descriptorInfo(),
      maxReductionBuffer0->descriptorInfo()};
  reductionSet0 =
      context.vulkan.globalDescriptorAllocator->allocate(reductionSetLayout0);
  writer.clear();
  writer.writeBuffer(0, reductionInfos0[0], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, reductionInfos0[1], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, reductionInfos0[2], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(reductionSet0);

  VkDescriptorBufferInfo reductionInfos1[] = {
      minReductionBuffer0->descriptorInfo(),
      maxReductionBuffer0->descriptorInfo(),
      minReductionBuffer1->descriptorInfo(),
      maxReductionBuffer1->descriptorInfo()};
  reductionSet1 =
      context.vulkan.globalDescriptorAllocator->allocate(reductionSetLayout1);
  writer.clear();
  writer.writeBuffer(0, reductionInfos1[0], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, reductionInfos1[1], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, reductionInfos1[2], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(3, reductionInfos1[3], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(reductionSet1);

  VkDescriptorBufferInfo reductionInfos2[] = {
      minReductionBuffer1->descriptorInfo(),
      maxReductionBuffer1->descriptorInfo(),
      minReductionBuffer2->descriptorInfo(),
      maxReductionBuffer2->descriptorInfo()};
  reductionSet2 =
      context.vulkan.globalDescriptorAllocator->allocate(reductionSetLayout1);
  writer.clear();
  writer.writeBuffer(0, reductionInfos2[0], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, reductionInfos2[1], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, reductionInfos2[2], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(3, reductionInfos2[3], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(reductionSet2);

  bindings.emplace_back(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1,
                        VK_SHADER_STAGE_COMPUTE_BIT);
  updateSetLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(updateSetLayout),
                    "WaterSys update descriptor set layout");
  VkDescriptorBufferInfo updateInfos[] = {
      minReductionBuffer2->descriptorInfo(),
      maxReductionBuffer2->descriptorInfo(), masterMinBuffer->descriptorInfo(),
      masterMaxBuffer->descriptorInfo(), scaleBuffer->descriptorInfo()};
  updateSet =
      context.vulkan.globalDescriptorAllocator->allocate(updateSetLayout);
  writer.clear();
  writer.writeBuffer(0, updateInfos[0], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, updateInfos[1], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(2, updateInfos[2], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(3, updateInfos[3], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(4, updateInfos[4], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(updateSet);

  bindings = {VkDescriptorSetLayoutBinding{
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
              }};
  // Normalize descriptor set layout
  normalizeSetLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(normalizeSetLayout),
                    "WaterSys normalize descriptor set layout");
  VkDescriptorBufferInfo normalizeInfos[] = {
      displacementsAndNormals->descriptorInfo(), scaleBuffer->descriptorInfo()};
  normalizeSet =
      context.vulkan.globalDescriptorAllocator->allocate(normalizeSetLayout);
  writer.clear();
  writer.writeBuffer(0, normalizeInfos[0], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.writeBuffer(1, normalizeInfos[1], VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(normalizeSet);
}

WSTessendorf::WSTessendorf(EngineContext &context) : System(context) {
  VkFenceCreateInfo fenceCreateInfo{};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  vkCreateFence(context.vulkan.device, &fenceCreateInfo, nullptr, &fence);

  FFTData = std::make_unique<Buffer<std::complex<float>>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      7 * tileSizeSquared);

  baseWaveHeightField = std::make_unique<Buffer<BaseWaveHeight>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared);

  waveVectors = std::make_unique<Buffer<WaveVector>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared);

  displacementsAndNormals = std::make_unique<Buffer<glm::vec4>>(
      context,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared * 2);

  // Initialize reduction buffers
  const uint32_t reductionSize0 = tileSizeSquared / 256;
  minReductionBuffer0 = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reductionSize0);
  maxReductionBuffer0 = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reductionSize0);

  const uint32_t reductionSize1 = reductionSize0 / 256;
  minReductionBuffer1 = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reductionSize1);
  maxReductionBuffer1 = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, reductionSize1);

  minReductionBuffer2 = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      1);
  maxReductionBuffer2 = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      1);

  masterMinBuffer = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      1);
  masterMaxBuffer = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      1);
  scaleBuffer = std::make_unique<Buffer<float>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 1);

  float initMin = std::numeric_limits<float>::max();
  masterMinBuffer->map();
  masterMinBuffer->write(&initMin);
  masterMinBuffer->unmap();

  float initMax = -std::numeric_limits<float>::max();
  masterMaxBuffer->map();
  masterMaxBuffer->write(&initMax);
  masterMaxBuffer->unmap();

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
  config.numberBatches = 7;
  config.useLUT = 0;
  config.performR2C = 0;
  config.performDCT = 0;
  config.disableReorderFourStep = 0;

  uint64_t bufSize =
      7 * config.size[0] * config.size[1] * sizeof(std::complex<float>);
  config.buffer = *FFTData;
  config.bufferSize = &bufSize;
  config.bufferNum = 1;
  config.makeInversePlanOnly = 1;

  VkFFTResult result = initializeVkFFT(&app, config);
  if (result)
    throw std::runtime_error(
        std::format("couldnt init VkFFT, error {}", static_cast<int>(result)));

  createDescriptors();

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
      "water pre FFT pipeline");

  FFTLayoutInfo.pSetLayouts = &postFFTSetLayout;
  postFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/postFFT.comp.spv", FFTLayoutInfo,
      "water post FFT pipeline");

  // Create reduction pipelines
  VkPushConstantRange reductionPushConstant{};
  reductionPushConstant.size = sizeof(ReductionPushConstants);
  reductionPushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkPipelineLayoutCreateInfo reductionLayoutInfo{};
  reductionLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  reductionLayoutInfo.setLayoutCount = 1;
  reductionLayoutInfo.pushConstantRangeCount = 1;
  reductionLayoutInfo.pPushConstantRanges = &reductionPushConstant;

  reductionLayoutInfo.pSetLayouts = &reductionSetLayout0;
  reductionPipeline0 = std::make_unique<ComputePipeline>(
      context, "shaders/water/reduction_stage0.comp.spv", reductionLayoutInfo,
      "water reduction pipeline stage 0");

  reductionLayoutInfo.pSetLayouts = &reductionSetLayout1;
  reductionPipeline1 = std::make_unique<ComputePipeline>(
      context, "shaders/water/reduction_stage1.comp.spv", reductionLayoutInfo,
      "water reduction pipeline stage 1");

  VkPipelineLayoutCreateInfo updateLayoutInfo{};
  updateLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  updateLayoutInfo.setLayoutCount = 1;
  updateLayoutInfo.pSetLayouts = &updateSetLayout;
  updatePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/update_master.comp.spv", updateLayoutInfo,
      "water update pipeline");

  VkPushConstantRange normalizePushConstant{};
  normalizePushConstant.size = sizeof(uint32_t);
  normalizePushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkPipelineLayoutCreateInfo normalizeLayoutInfo{};
  normalizeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  normalizeLayoutInfo.setLayoutCount = 1;
  normalizeLayoutInfo.pSetLayouts = &normalizeSetLayout;
  normalizeLayoutInfo.pushConstantRangeCount = 1;
  normalizeLayoutInfo.pPushConstantRanges = &normalizePushConstant;
  normalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/normalize.comp.spv", normalizeLayoutInfo,
      "water normalize pipeline");

  VkPipelineLayoutCreateInfo baseWaveHeightFieldLayoutInfo{};
  baseWaveHeightFieldLayoutInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  baseWaveHeightFieldLayoutInfo.setLayoutCount = 1;
  baseWaveHeightFieldLayoutInfo.pSetLayouts = &preFFTSetLayout;
  ComputePipeline baseWaveHeightFieldPipeline(
      context, "shaders/water/baseWaveHeightField.comp.spv",
      baseWaveHeightFieldLayoutInfo, "water base wave height field pipeline");

  // SetWindDirection(defaultWindDir);
  // m_WindSpeed = glm::max(0.0001f, defaultWindSpeed);
  // SetAnimationPeriod(200.f);

  std::vector<WaveVector> waveVecs;
  computeWaveVectors(waveVecs);
  waveVectors->map();
  waveVectors->write(waveVecs.data());
  waveVectors->unmap();

  auto cmd = beginSingleTimeCommands(context);
  baseWaveHeightFieldPipeline.bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          baseWaveHeightFieldPipeline, 0, 1, &preFFTSet, 0,
                          nullptr);
  vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);
  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);

  // baseWaveHeightField->map();
  // baseWaveHeightField->write(computeBaseWaveHeightField(waveVecs).data());
  // baseWaveHeightField->unmap();
}

WSTessendorf::~WSTessendorf() {
  deleteVkFFT(&app);
  vkDestroyFence(context.vulkan.device, fence, nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, preFFTSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, postFFTSetLayout,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, reductionSetLayout0,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, reductionSetLayout1,
                               nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, updateSetLayout, nullptr);
  vkDestroyDescriptorSetLayout(context.vulkan.device, normalizeSetLayout,
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

// std::vector<WSTessendorf::BaseWaveHeight>
// WSTessendorf::computeBaseWaveHeightField(
//     const std::vector<WaveVector> waveVectors) const {
//   std::vector<WSTessendorf::BaseWaveHeight> baseWaveHeights(tileSize *
//                                                             tileSize);
//   assert(waveVectors.size() == baseWaveHeights.size());
//
//   for (uint32_t m = 0; m < tileSize; ++m) {
//     for (uint32_t n = 0; n < tileSize; ++n) {
//       const uint32_t kIndex = m * tileSize + n;
//       const auto &kWaveVec = waveVectors[kIndex];
//       const float k = glm::length(kWaveVec.vec);
//       auto &h0 = baseWaveHeights[kIndex];
//       if (k > std::numeric_limits<float>::epsilon()) {
//         const auto gaussRandom = std::complex<float>(
//             glm::gaussRand(0.0f, 1.0f), glm::gaussRand(0.0f, 1.0f));
// h0.heightAmp = baseWaveHeightFT(gaussRandom, kWaveVec.unit, k);
// h0.heightAmp_conj =
// std::conj(baseWaveHeightFT(gaussRandom, -kWaveVec.unit, k));
//       h0.dispersion = QDispersion(k);
//     } else {
//       h0.heightAmp = std::complex<float>(0);
//       h0.heightAmp_conj = std::conj(std::complex<float>(0));
//       h0.dispersion = 0.0f;
//     }
//   }
//   }
//   return baseWaveHeights;
// }

float WSTessendorf::recordComputeWaves(VkCommandBuffer &cmd, float t) {
  debug::beginLabel(context, cmd, "record compute waves", {.2f, .2f, 1.f, 1.f});
  {
    PushConstantData data{t};
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
      vkCmdPushConstants(cmd, postFFTPipeline->getLayout(),
                         VK_SHADER_STAGE_COMPUTE_BIT, 0,
                         sizeof(PushConstantData), &data);
      postFFTPipeline->bind(cmd);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              postFFTPipeline->getLayout(), 0, 1, &postFFTSet,
                              0, nullptr);
      vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);
    }
    debug::endLabel(context, cmd);

    debug::beginLabel(context, cmd, "reduction", {.2f, .2f, 1.f, 1.f});
    {
      // Reduction Stage 0: Reduce 262,144 heights to 1024 min/max values
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              reductionPipeline0->getLayout(), 0, 1,
                              &reductionSet0, 0, nullptr);
      uint32_t inputSize = tileSizeSquared;
      vkCmdPushConstants(cmd, reductionPipeline0->getLayout(),
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t),
                         &inputSize);
      reductionPipeline0->bind(cmd);
      uint32_t workgroupCount = (tileSizeSquared + 255) / 256; // 1024
      vkCmdDispatch(cmd, workgroupCount, 1, 1);

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                           &memoryBarrier, 0, nullptr, 0, nullptr);

      // Reduction Stage 1: Reduce 1024 min/max values to 4
      reductionPipeline1->bind(cmd);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              reductionPipeline1->getLayout(), 0, 1,
                              &reductionSet1, 0, nullptr);
      inputSize = workgroupCount;
      workgroupCount = (inputSize + 255) / 256;
      vkCmdPushConstants(cmd, reductionPipeline1->getLayout(),
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t),
                         &inputSize);
      vkCmdDispatch(cmd, workgroupCount, 1, 1);

      vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                           VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                           &memoryBarrier, 0, nullptr, 0, nullptr);

      // Reduction Stage 2: Reduce 4 min/max values to 1
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              reductionPipeline1->getLayout(), 0, 1,
                              &reductionSet2, 0, nullptr);
      inputSize = workgroupCount;
      workgroupCount = 1;
      vkCmdPushConstants(cmd, reductionPipeline1->getLayout(),
                         VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t),
                         &inputSize);
      vkCmdDispatch(cmd, workgroupCount, 1, 1);
    }
    debug::endLabel(context, cmd);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr);

    debug::beginLabel(context, cmd, "update master min/max",
                      {.2f, .2f, 1.f, 1.f});
    {
      updatePipeline->bind(cmd);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              updatePipeline->getLayout(), 0, 1, &updateSet, 0,
                              nullptr);
      vkCmdDispatch(cmd, 1, 1, 1);
    }
    debug::endLabel(context, cmd);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr);

    debug::beginLabel(context, cmd, "normalize displacements",
                      {.2f, .2f, 1.f, 1.f});
    {
      normalizePipeline->bind(cmd);
      vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                              normalizePipeline->getLayout(), 0, 1,
                              &normalizeSet, 0, nullptr);
      vkCmdDispatch(cmd, (tileSize + 15) / 16, (tileSize + 15) / 16, 1);
    }
    debug::endLabel(context, cmd);

    vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                         VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                         &memoryBarrier, 0, nullptr, 0, nullptr);
  }
  debug::endLabel(context, cmd);

  maxReductionBuffer2->map();
  float maxHeight;
  memcpy(&maxHeight, maxReductionBuffer2->getMappedAddr(), sizeof(float));
  maxReductionBuffer2->unmap();
  minReductionBuffer2->map();
  float minHeight;
  memcpy(&minHeight, minReductionBuffer2->getMappedAddr(), sizeof(float));
  minReductionBuffer2->unmap();
  const float A = glm::max(glm::abs(minHeight), glm::abs(maxHeight));
  return A;
}
} // namespace vkh
