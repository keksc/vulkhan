#include "WSTessendorf.hpp"

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../image.hpp"
#include "../../pipeline.hpp"
#include <format>
#include <stdexcept>
#include <vulkan/vulkan.hpp>

namespace vkh {

void WSTessendorf::createDescriptors() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
      vk::DescriptorSetLayoutBinding{2, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
  };
  preFFTSetLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkDescriptorSetLayout>(preFFTSetLayout)),
                    "WaterSys pre FFT descriptor set layout");
  preFFTSet =
      context.vulkan.globalDescriptorAllocator->allocate(preFFTSetLayout);

  DescriptorWriter writer(context);
  writer.writeBuffer(0, FFTData->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(1, baseWaveHeightField->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(2, waveVectors->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.updateSet(preFFTSet);

  std::vector<vk::DescriptorSetLayoutBinding> postBindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageImage, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr}};

  postFFTSetLayout = buildDescriptorSetLayout(context, postBindings);
  debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkDescriptorSetLayout>(postFFTSetLayout)),
                    "WaterSys post FFT descriptor set layout");
  postFFTSet =
      context.vulkan.globalDescriptorAllocator->allocate(postFFTSetLayout);
  writer.clear();
  writer.writeBuffer(0, FFTData->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeImage(
      1, displacementFoamMap->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  writer.updateSet(postFFTSet);
}

WSTessendorf::WSTessendorf(EngineContext &context) : System(context) {
  vk::FenceCreateInfo fenceCreateInfo{};
  if (context.vulkan.device.createFence(&fenceCreateInfo, nullptr, &fence) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create fence");
  }

  FFTData = std::make_unique<Buffer<std::complex<float>>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal, 3 * tileSizeSquared);

  baseWaveHeightField = std::make_unique<Buffer<BaseWaveHeight>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal, tileSizeSquared);

  waveVectors = std::make_unique<Buffer<WaveVector>>(
      context,
      vk::BufferUsageFlagBits::eTransferDst |
          vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal, tileSizeSquared);

  ImageCreateInfo_empty createInfo{};
  createInfo.size = glm::uvec2{tileSize};
  createInfo.format = vk::Format::eR16G16B16A16Sfloat;
  createInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eStorage;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.name = "water displacement map";
  displacementFoamMap = std::make_unique<Image>(context, createInfo);

  // VkFFT Interop configuration using raw handles extracted via casting
  // wrappers
  VkFFTConfiguration config{};
  VkPhysicalDevice rawPhysDevice = context.vulkan.physicalDevice;
  VkDevice rawDevice = context.vulkan.device;
  VkQueue rawComputeQueue = context.vulkan.computeQueue;
  VkCommandPool rawCmdPool = context.vulkan.commandPool;
  VkFence rawFence = fence;

  config.physicalDevice = &rawPhysDevice;
  config.device = &rawDevice;
  config.queue = &rawComputeQueue;
  config.commandPool = &rawCmdPool;
  config.isCompilerInitialized = 0;
  config.fence = &rawFence;
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
  VkBuffer rawBuffer = static_cast<vk::Buffer>(*FFTData);
  config.buffer = &rawBuffer;
  config.bufferSize = &bufSize;
  config.bufferNum = 1;
  config.makeInversePlanOnly = 1;

  VkFFTResult result = initializeVkFFT(&app, config);
  if (result)
    throw std::runtime_error(
        std::format("couldnt init VkFFT, error {}", static_cast<int>(result)));

  createDescriptors();

  vk::SpecializationMapEntry specMapEntry{0, 0, sizeof(uint32_t)};

  vk::SpecializationInfo specInfo{1, &specMapEntry, sizeof(uint32_t),
                                  &tileSize};

  vk::PipelineLayoutCreateInfo FFTLayoutInfo{};
  FFTLayoutInfo.setLayoutCount = 1;
  FFTLayoutInfo.pSetLayouts = &preFFTSetLayout;

  vk::PushConstantRange pushConstantRange{vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(PushConstantData)};
  FFTLayoutInfo.pushConstantRangeCount = 1;
  FFTLayoutInfo.pPushConstantRanges = &pushConstantRange;

  preFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/preFFT.comp.spv", FFTLayoutInfo,
      "water pre FFT pipeline", &specInfo);

  FFTLayoutInfo.pSetLayouts = &postFFTSetLayout;
  postFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/postFFT.comp.spv", FFTLayoutInfo,
      "water post FFT pipeline", &specInfo);

  vk::PipelineLayoutCreateInfo baseWaveHeightFieldLayoutInfo{};
  baseWaveHeightFieldLayoutInfo.setLayoutCount = 1;
  baseWaveHeightFieldLayoutInfo.pSetLayouts = &preFFTSetLayout;

  ComputePipeline baseWaveHeightFieldPipeline(
      context, "shaders/water/baseWaveHeightField.comp.spv",
      baseWaveHeightFieldLayoutInfo, "water base wave height field pipeline",
      &specInfo);

  Buffer<WaveVector> stagingBuffer(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      tileSizeSquared);

  std::vector<WaveVector> waveVecs;
  computeWaveVectors(waveVecs);
  stagingBuffer.map();
  stagingBuffer.write(waveVecs.data());
  stagingBuffer.unmap();

  auto cmd = beginSingleTimeCommands(context);
  waveVectors->recordCopyFromBuffer(cmd, stagingBuffer);
  baseWaveHeightFieldPipeline.bind(cmd);

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         baseWaveHeightFieldPipeline.getLayout(), 0, 1,
                         &preFFTSet, 0, nullptr);
  cmd.dispatch(tileSize / 16, tileSize / 16, 1);
  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);
}

WSTessendorf::~WSTessendorf() {
  deleteVkFFT(&app);
  if (context.vulkan.device) {
    context.vulkan.device.destroyFence(fence, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(preFFTSetLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(postFFTSetLayout, nullptr);
  }
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

void WSTessendorf::recordComputeWaves(vk::CommandBuffer &cmd, float t) {
  debug::beginLabel(context, cmd, "Compute waves", {.2f, .2f, 1.f, 1.f});
  {
    PushConstantData data{.t = t, .dt = context.frameInfo.dt};
    debug::beginLabel(context, cmd, "pre FFT", {.2f, .2f, 1.f, 1.f});
    {
      cmd.pushConstants(preFFTPipeline->getLayout(),
                        vk::ShaderStageFlagBits::eCompute, 0,
                        sizeof(PushConstantData), &data);
      preFFTPipeline->bind(cmd);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             preFFTPipeline->getLayout(), 0, 1, &preFFTSet, 0,
                             nullptr);
      cmd.dispatch(tileSize / 16, tileSize / 16, 1);
    }
    debug::endLabel(context, cmd);

    vk::MemoryBarrier memoryBarrier{vk::AccessFlagBits::eShaderWrite,
                                    vk::AccessFlagBits::eShaderRead};

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{}, 1, &memoryBarrier, 0, nullptr, 0,
                        nullptr);

    debug::beginLabel(context, cmd, "FFT", {.2f, .2f, 1.f, 1.f});
    {
      VkFFTLaunchParams launchParams{};
      VkBuffer rawBuffer = static_cast<vk::Buffer>(*FFTData);
      VkCommandBuffer rawCmd = cmd;
      launchParams.buffer = &rawBuffer;
      launchParams.commandBuffer = &rawCmd;
      VkFFTResult result = VkFFTAppend(&app, 1, &launchParams);
    }
    debug::endLabel(context, cmd);

    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{}, 1, &memoryBarrier, 0, nullptr, 0,
                        nullptr);

    debug::beginLabel(context, cmd, "post FFT", {.2f, .2f, 1.f, 1.f});
    {
      displacementFoamMap->recordTransitionLayout(cmd,
                                                  vk::ImageLayout::eGeneral);

      cmd.pushConstants(postFFTPipeline->getLayout(),
                        vk::ShaderStageFlagBits::eCompute, 0,
                        sizeof(PushConstantData), &data);
      postFFTPipeline->bind(cmd);
      cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                             postFFTPipeline->getLayout(), 0, 1, &postFFTSet, 0,
                             nullptr);
      cmd.dispatch(tileSize / 16, tileSize / 16, 1);

      displacementFoamMap->recordTransitionLayout(
          cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
    }
    debug::endLabel(context, cmd);
  }
  debug::endLabel(context, cmd);
}

} // namespace vkh
