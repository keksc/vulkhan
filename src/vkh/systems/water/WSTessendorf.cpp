#include "WSTessendorf.hpp"

#include <cstring>
#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <limits>
#include <memory>
#include <stdexcept>

#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"

namespace vkh {
void WSTessendorf::createDescriptors() {
  preFFTSetLayout = DescriptorSetLayout::Builder(context)
                        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT)
                        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT)
                        .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT)
                        .build();
  VkDescriptorBufferInfo preFFTDescriptorInfos[] = {
      FFTData->descriptorInfo(), baseWaveHeightField->descriptorInfo(),
      waveVectors->descriptorInfo()};
  DescriptorWriter(*preFFTSetLayout, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &preFFTDescriptorInfos[0])
      .writeBuffer(1, &preFFTDescriptorInfos[1])
      .writeBuffer(2, &preFFTDescriptorInfos[2])
      .build(preFFTSet);

  postFFTSetLayout = DescriptorSetLayout::Builder(context)
                         .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_SHADER_STAGE_COMPUTE_BIT)
                         .build();
  VkDescriptorBufferInfo postFFTDescriptorInfos[] = {
      FFTData->descriptorInfo(), displacementsAndNormals->descriptorInfo(),
      displacementsAndNormals->descriptorInfo()};
  DescriptorWriter(*postFFTSetLayout, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &postFFTDescriptorInfos[0])
      .writeBuffer(1, &postFFTDescriptorInfos[1])
      .writeBuffer(2, &postFFTDescriptorInfos[2])
      .build(postFFTSet);

  // Reduction descriptor set layout for stage 0
  reductionSetLayout0 =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // displacements
          .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // minOut
          .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // maxOut
          .build();

  // Reduction descriptor set layout for subsequent stages
  reductionSetLayout1 = DescriptorSetLayout::Builder(context)
                            .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        VK_SHADER_STAGE_COMPUTE_BIT) // minIn
                            .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        VK_SHADER_STAGE_COMPUTE_BIT) // maxIn
                            .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        VK_SHADER_STAGE_COMPUTE_BIT) // minOut
                            .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                        VK_SHADER_STAGE_COMPUTE_BIT) // maxOut
                            .build();

  // Create reduction descriptor sets
  VkDescriptorBufferInfo reductionInfos0[] = {
      displacementsAndNormals->descriptorInfo(),
      minReductionBuffer0->descriptorInfo(),
      maxReductionBuffer0->descriptorInfo()};
  DescriptorWriter(*reductionSetLayout0, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &reductionInfos0[0])
      .writeBuffer(1, &reductionInfos0[1])
      .writeBuffer(2, &reductionInfos0[2])
      .build(reductionSet0);

  VkDescriptorBufferInfo reductionInfos1[] = {
      minReductionBuffer0->descriptorInfo(),
      maxReductionBuffer0->descriptorInfo(),
      minReductionBuffer1->descriptorInfo(),
      maxReductionBuffer1->descriptorInfo()};
  DescriptorWriter(*reductionSetLayout1, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &reductionInfos1[0])
      .writeBuffer(1, &reductionInfos1[1])
      .writeBuffer(2, &reductionInfos1[2])
      .writeBuffer(3, &reductionInfos1[3])
      .build(reductionSet1);

  VkDescriptorBufferInfo reductionInfos2[] = {
      minReductionBuffer1->descriptorInfo(),
      maxReductionBuffer1->descriptorInfo(),
      minReductionBuffer2->descriptorInfo(),
      maxReductionBuffer2->descriptorInfo()};
  DescriptorWriter(*reductionSetLayout1, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &reductionInfos2[0])
      .writeBuffer(1, &reductionInfos2[1])
      .writeBuffer(2, &reductionInfos2[2])
      .writeBuffer(3, &reductionInfos2[3])
      .build(reductionSet2);

  updateSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // minReductionBuffer2
          .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // maxReductionBuffer2
          .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // masterMinBuffer
          .addBinding(3, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // masterMaxBuffer
          .addBinding(4, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // scaleBuffer
          .build();

  VkDescriptorBufferInfo updateInfos[] = {
      minReductionBuffer2->descriptorInfo(),
      maxReductionBuffer2->descriptorInfo(), masterMinBuffer->descriptorInfo(),
      masterMaxBuffer->descriptorInfo(), scaleBuffer->descriptorInfo()};
  DescriptorWriter(*updateSetLayout, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &updateInfos[0])
      .writeBuffer(1, &updateInfos[1])
      .writeBuffer(2, &updateInfos[2])
      .writeBuffer(3, &updateInfos[3])
      .writeBuffer(4, &updateInfos[4])
      .build(updateSet);

  // Normalize descriptor set layout
  normalizeSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // displacements
          .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                      VK_SHADER_STAGE_COMPUTE_BIT) // scaleBuffer
          .build();

  VkDescriptorBufferInfo normalizeInfos[] = {
      displacementsAndNormals->descriptorInfo(), scaleBuffer->descriptorInfo()};
  DescriptorWriter(*normalizeSetLayout, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &normalizeInfos[0])
      .writeBuffer(1, &normalizeInfos[1])
      .build(normalizeSet);
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
        fmt::format("couldnt init VkFFT, error {}", static_cast<int>(result)));

  createDescriptors();

  VkPipelineLayoutCreateInfo FFTLayoutInfo{};
  FFTLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  FFTLayoutInfo.setLayoutCount = 1;
  FFTLayoutInfo.pSetLayouts = *preFFTSetLayout;
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.size = sizeof(PushConstantData);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  FFTLayoutInfo.pushConstantRangeCount = 1;
  FFTLayoutInfo.pPushConstantRanges = &pushConstantRange;
  preFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/preFFT.comp.spv", FFTLayoutInfo);

  FFTLayoutInfo.pSetLayouts = *postFFTSetLayout;
  postFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/postFFT.comp.spv", FFTLayoutInfo);

  // Create reduction pipelines
  VkPushConstantRange reductionPushConstant{};
  reductionPushConstant.size = sizeof(ReductionPushConstants);
  reductionPushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkPipelineLayoutCreateInfo reductionLayoutInfo{};
  reductionLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  reductionLayoutInfo.setLayoutCount = 1;
  reductionLayoutInfo.pushConstantRangeCount = 1;
  reductionLayoutInfo.pPushConstantRanges = &reductionPushConstant;

  reductionLayoutInfo.pSetLayouts = *reductionSetLayout0;
  reductionPipeline0 = std::make_unique<ComputePipeline>(
      context, "shaders/water/reduction_stage0.comp.spv", reductionLayoutInfo);

  reductionLayoutInfo.pSetLayouts = *reductionSetLayout1;
  reductionPipeline1 = std::make_unique<ComputePipeline>(
      context, "shaders/water/reduction_stage1.comp.spv", reductionLayoutInfo);

  VkPipelineLayoutCreateInfo updateLayoutInfo{};
  updateLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  updateLayoutInfo.setLayoutCount = 1;
  updateLayoutInfo.pSetLayouts = *updateSetLayout;
  updatePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/update_master.comp.spv", updateLayoutInfo);

  VkPushConstantRange normalizePushConstant{};
  normalizePushConstant.size = sizeof(uint32_t);
  normalizePushConstant.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  VkPipelineLayoutCreateInfo normalizeLayoutInfo{};
  normalizeLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  normalizeLayoutInfo.setLayoutCount = 1;
  normalizeLayoutInfo.pSetLayouts = *normalizeSetLayout;
  normalizeLayoutInfo.pushConstantRangeCount = 1;
  normalizeLayoutInfo.pPushConstantRanges = &normalizePushConstant;
  normalizePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/normalize.comp.spv", normalizeLayoutInfo);

  VkPipelineLayoutCreateInfo baseWaveHeightFieldLayoutInfo{};
  baseWaveHeightFieldLayoutInfo.sType =
      VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  baseWaveHeightFieldLayoutInfo.setLayoutCount = 1;
  baseWaveHeightFieldLayoutInfo.pSetLayouts = *preFFTSetLayout;
  ComputePipeline baseWaveHeightFieldPipeline(
      context, "shaders/water/baseWaveHeightField.comp.spv",
      baseWaveHeightFieldLayoutInfo);

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
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, baseWaveHeightFieldPipeline, 0, 1, &preFFTSet, 0, nullptr);
  vkCmdDispatch(cmd, tileSize/16, tileSize/16, 1);
  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);

  // baseWaveHeightField->map();
  // baseWaveHeightField->write(computeBaseWaveHeightField(waveVecs).data());
  // baseWaveHeightField->unmap();
}

WSTessendorf::~WSTessendorf() {
  deleteVkFFT(&app);
  vkDestroyFence(context.vulkan.device, fence, nullptr);
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

float WSTessendorf::computeWaves(float t) {
  auto cmd = beginSingleTimeCommands(context);

  PushConstantData data{t};
  vkCmdPushConstants(cmd, preFFTPipeline->getLayout(),
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantData),
                     &data);
  preFFTPipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          preFFTPipeline->getLayout(), 0, 1, &preFFTSet, 0,
                          nullptr);
  vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);

  VkMemoryBarrier memoryBarrier{};
  memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  VkFFTLaunchParams launchParams{};
  launchParams.buffer = *FFTData;
  launchParams.commandBuffer = &cmd;
  VkFFTResult result = VkFFTAppend(&app, 1, &launchParams);

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  vkCmdPushConstants(cmd, postFFTPipeline->getLayout(),
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantData),
                     &data);
  postFFTPipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          postFFTPipeline->getLayout(), 0, 1, &postFFTSet, 0,
                          nullptr);
  vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);

  // Reduction Stage 0: Reduce 262,144 heights to 1024 min/max values
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, *reductionPipeline0);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reductionPipeline0->getLayout(), 0, 1, &reductionSet0,
                          0, nullptr);
  uint32_t inputSize = tileSizeSquared;
  vkCmdPushConstants(cmd, reductionPipeline0->getLayout(),
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t),
                     &inputSize);
  uint32_t workgroupCount = (tileSizeSquared + 255) / 256; // 1024
  vkCmdDispatch(cmd, workgroupCount, 1, 1);

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  // Reduction Stage 1: Reduce 1024 min/max values to 4
  vkCmdBindPipeline(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, *reductionPipeline1);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          reductionPipeline1->getLayout(), 0, 1, &reductionSet1,
                          0, nullptr);
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
                          reductionPipeline1->getLayout(), 0, 1, &reductionSet2,
                          0, nullptr);
  inputSize = workgroupCount;
  workgroupCount = 1;
  vkCmdPushConstants(cmd, reductionPipeline1->getLayout(),
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(uint32_t),
                     &inputSize);
  vkCmdDispatch(cmd, workgroupCount, 1, 1);

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  // Update master min/max and compute scale
  updatePipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          updatePipeline->getLayout(), 0, 1, &updateSet, 0,
                          nullptr);
  vkCmdDispatch(cmd, 1, 1, 1);

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  // Normalize displacements on GPU
  normalizePipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          normalizePipeline->getLayout(), 0, 1, &normalizeSet,
                          0, nullptr);
  vkCmdDispatch(cmd, (tileSize + 15) / 16, (tileSize + 15) / 16, 1);

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);

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
