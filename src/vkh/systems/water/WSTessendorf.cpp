#include "WSTessendorf.hpp"

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "water.hpp" // getGlobalWindDirection()/getGlobalWindStrength()
#include <array>
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

  // Named locals, not temporaries: DescriptorWriter's VkWriteDescriptorSet
  // entries point at the address of whatever info struct you pass in
  // rather than copying it, so these have to stay alive until updateSet()
  // below actually runs. Passing e.g. FFTData->descriptorInfo() directly
  // as an rvalue leaves pBufferInfo dangling the instant that statement
  // ends - vkUpdateDescriptorSets then reads freed stack memory and
  // segfaults (this is exactly what was happening here).
  VkDescriptorBufferInfo fftDataInfo = FFTData->descriptorInfo();
  VkDescriptorBufferInfo baseWaveHeightFieldInfo =
      baseWaveHeightField->descriptorInfo();
  VkDescriptorBufferInfo waveVectorsInfo = waveVectors->descriptorInfo();

  DescriptorWriter writer(context);
  writer.writeBuffer(0, fftDataInfo, vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(1, baseWaveHeightFieldInfo,
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(2, waveVectorsInfo, vk::DescriptorType::eStorageBuffer);
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

  // Same reasoning as above - keep these alive until this second
  // updateSet() call runs.
  VkDescriptorBufferInfo fftDataInfoPost = FFTData->descriptorInfo();
  VkDescriptorImageInfo displacementImageInfo =
      displacementFoamMap->getDescriptorInfo(context.vulkan.defaultSampler);

  writer.writeBuffer(0, fftDataInfoPost, vk::DescriptorType::eStorageBuffer);
  writer.writeImage(1, displacementImageInfo,
                    vk::DescriptorType::eStorageImage);
  writer.updateSet(postFFTSet);
}

WSTessendorf::WSTessendorf(EngineContext &context, float patchLength)
    : System(context), patchLength(patchLength) {
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
  // eTransferSrc/eTransferDst added: recordGenerateMips() blits each mip
  // level from the one above it (eTransferSrc to read level N,
  // eTransferDst to write level N+1) - eStorage alone (needed for
  // postFFT.comp's imageStore into mip 0) doesn't cover that.
  createInfo.usage = vk::ImageUsageFlagBits::eTransferSrc |
                     vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eStorage;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.mipLevels = mipLevelCount;
  createInfo.name = "water displacement map";
  displacementFoamMap = std::make_unique<Image2D>(context, createInfo);

  // VkFFT Interop configuration using raw handles extracted via casting
  // wrappers. These live in member variables (vkfft*), NOT locals - see
  // the members' declaration in WSTessendorf.hpp for why: VkFFT keeps
  // these pointers around and dereferences them again on every
  // VkFFTAppend() call, long after this constructor has returned.
  VkFFTConfiguration config{};
  vkfftPhysicalDevice = context.vulkan.physicalDevice;
  vkfftDevice = context.vulkan.device;
  vkfftComputeQueue = context.vulkan.computeQueue;
  vkfftCommandPool = context.vulkan.commandPool;
  vkfftFence = fence;

  config.physicalDevice = &vkfftPhysicalDevice;
  config.device = &vkfftDevice;
  config.queue = &vkfftComputeQueue;
  config.commandPool = &vkfftCommandPool;
  config.isCompilerInitialized = 0;
  config.fence = &vkfftFence;
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
  config.normalize = 0;

  vkfftBufferSize =
      3 * config.size[0] * config.size[1] * sizeof(std::complex<float>);
  vkfftBuffer = static_cast<vk::Buffer>(*FFTData);
  config.buffer = &vkfftBuffer;
  config.bufferSize = &vkfftBufferSize;
  config.bufferNum = 1;
  config.makeInversePlanOnly = 1;

  VkFFTResult result = initializeVkFFT(&app, config);
  if (result)
    throw std::runtime_error(
        std::format("couldnt init VkFFT, error {}", static_cast<int>(result)));

  createDescriptors();

  const std::array<uint32_t, 2> specData{
      tileSize, patchLength == detailTileLength ? 1u : 0u};
  const std::array<vk::SpecializationMapEntry, 2> specEntries{
      vk::SpecializationMapEntry{0, 0, sizeof(uint32_t)},
      vk::SpecializationMapEntry{1, sizeof(uint32_t), sizeof(uint32_t)}};
  vk::SpecializationInfo specInfo{static_cast<uint32_t>(specEntries.size()),
                                  specEntries.data(), sizeof(specData),
                                  specData.data()};

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

  vk::PushConstantRange windPushConstantRange{vk::ShaderStageFlagBits::eCompute,
                                              0, sizeof(WindPushConstantData)};
  baseWaveHeightFieldLayoutInfo.pushConstantRangeCount = 1;
  baseWaveHeightFieldLayoutInfo.pPushConstantRanges = &windPushConstantRange;

  baseWaveHeightFieldPipeline = std::make_unique<ComputePipeline>(
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
  recordUpdateSpectrumIfWindChanged(cmd);
  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);
  if (patchLength != detailTileLength) {
    detailCascade = std::make_unique<WSTessendorf>(context, detailTileLength);
  }
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
          glm::vec2(glm::pi<float>() * (2.0f * n - tileSize) / patchLength,
                    glm::pi<float>() * (2.0f * m - tileSize) / patchLength));
    }
  }
}

void WSTessendorf::recordUpdateSpectrumIfWindChanged(vk::CommandBuffer &cmd) {
  const glm::vec2 windDir = safeWindDirection(context.environment);
  const float windStrength = spectrumWindStrength(context.environment);
  // Small epsilons rather than exact equality - direction/strength are
  // floats that can drift by rounding even when a caller isn't
  // meaningfully changing them, and re-dispatching this shader is not
  // free (it integrates jonswapSpectrum() over all tileSizeSquared wave
  // vectors) - not worth doing every frame over noise.
  constexpr float kDirEpsilon = 1e-4f;
  constexpr float kStrengthEpsilon = 1e-4f;
  const bool changed =
      glm::length(windDir - lastSpectrumWindDir) > kDirEpsilon ||
      glm::abs(windStrength - lastSpectrumWindStrength) > kStrengthEpsilon;
  if (!changed)
    return;

  lastSpectrumWindDir = windDir;
  lastSpectrumWindStrength = windStrength;

  // Maps the engine's abstract 0-1 wind "strength" (see
  // setGlobalWindStrength()'s own doc comment in water.hpp - it's a
  // shading-effect knob, not itself a physical speed) onto an actual m/s
  // value for the JONSWAP spectrum's wind term. This is its own judgment
  // call, same as setGlobalWindSpeed()'s m/h mapping is - there's no
  // physical law tying "shading strength" to "spectrum wind speed"
  // because they were never the same quantity to begin with. Reference
  // points chosen so strength == 1.0 exactly reproduces
  // baseWaveHeightField.comp's old hardcoded 20 m/s (so nothing changes
  // for anyone who was already running at full wind), and strength == 0.0
  // still leaves a small nonzero wind speed rather than a literal 0 -
  // jonswapSpectrum()'s L = windSpeed^2/g sits in a couple of
  // denominators, and a literal 0 m/s would make k_p = 1/L divide by
  // zero.
  constexpr float kMinSpectrumWindSpeed = 0.5f;  // m/s
  constexpr float kMaxSpectrumWindSpeed = 20.0f; // m/s
  // Clamped here, not just documented as [0, 1] at the call site -
  // glm::mix() does not clamp its own `t`, so a caller accidentally
  // passing e.g. 10.f (ten times past the intended range) would silently
  // extrapolate windSpeed far past kMaxSpectrumWindSpeed instead of
  // saturating at it, blowing up jonswapSpectrum()'s output (and with it
  // wave amplitude) well beyond anything downstream waveScale tuning
  // expects to be damping.
  const float clampedWindStrength = glm::clamp(windStrength, 0.f, 1.f);
  const WindPushConstantData windData{
      .windDir = windDir,
      .windSpeed = glm::mix(kMinSpectrumWindSpeed, kMaxSpectrumWindSpeed,
                            clampedWindStrength)};

  debug::beginLabel(context, cmd, "Update wave spectrum (wind changed)",
                    {.6f, .2f, 1.f, 1.f});
  cmd.pushConstants(baseWaveHeightFieldPipeline->getLayout(),
                    vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(WindPushConstantData), &windData);
  baseWaveHeightFieldPipeline->bind(cmd);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         baseWaveHeightFieldPipeline->getLayout(), 0, 1,
                         &preFFTSet, 0, nullptr);
  cmd.dispatch(tileSize / 16, tileSize / 16, 1);

  // preFFT.comp reads baseWaveHeightField (preFFTSet binding 1) right
  // after this - same frame, same command buffer, whenever this actually
  // dispatches - so needs a write->read barrier, same shape as the
  // existing pre/post FFT barrier below in recordComputeWaves().
  vk::MemoryBarrier memoryBarrier{vk::AccessFlagBits::eShaderWrite,
                                  vk::AccessFlagBits::eShaderRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags{}, 1, &memoryBarrier, 0, nullptr, 0,
                      nullptr);
  debug::endLabel(context, cmd);
}

void WSTessendorf::recordComputeWaves(vk::CommandBuffer &cmd, float t) {
  debug::beginLabel(context, cmd, "Compute waves", {.2f, .2f, 1.f, 1.f});
  {
    recordUpdateSpectrumIfWindChanged(cmd);

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
      // Whole-image transition (all mips, via the tracked `layout` member)
      // is correct here: at this point in every frame the whole resource
      // is uniformly in whatever recordGenerateMips() left it in last time
      // (eShaderReadOnlyOptimal, or eGeneral on the very first frame per
      // the constructor's createInfo.layout) - see recordGenerateMips()'s
      // own comment for why that invariant matters.
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
      // NOTE: no eShaderReadOnlyOptimal transition here anymore - only
      // mip 0 was just written, and recordGenerateMips() below now owns
      // getting the whole mip chain (0..mipLevelCount-1) into
      // eShaderReadOnlyOptimal, since it needs mip 0 in
      // eTransferSrcOptimal first anyway to blit from it.
    }
    debug::endLabel(context, cmd);

    recordGenerateMips(cmd);
  }
  debug::endLabel(context, cmd);
  if (detailCascade) {
    detailCascade->recordComputeWaves(cmd, t);
  }
}

void WSTessendorf::recordGenerateMips(vk::CommandBuffer &cmd) {
  debug::beginLabel(context, cmd, "Generate water mips", {.2f, .6f, 1.f, 1.f});

  // No neighbor mip to blit from - just resync straight to
  // eShaderReadOnlyOptimal. Not expected to trigger at tileSize == 512
  // (mipLevelCount == 10), but the loop below assumes at least one
  // iteration, so this guard keeps that assumption from becoming a
  // zero-length-range call below if mipLevelCount is ever 1.
  if (mipLevelCount <= 1) {
    displacementFoamMap->recordTransitionLayout(
        cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
    debug::endLabel(context, cmd);
    return;
  }

  // Invariant entering this function: the WHOLE image (all mip levels) is
  // uniformly eGeneral - postFFT.comp's imageStore just wrote mip 0 while
  // the whole resource was transitioned to eGeneral above, and mips
  // 1..mipLevelCount-1 were never separately touched since the last time
  // this same invariant held (so they're still physically eGeneral too).
  // This is what makes it safe to read displacementFoamMap's tracked
  // `layout` member as an accurate stand-in for "every subresource's real
  // layout" at this specific point - the whole point of the
  // recordTransitionMipLayout() overload below is that this DOESN'T stay
  // true for the interior of this function once mips start diverging.
  int32_t mipWidth = static_cast<int32_t>(tileSize);
  int32_t mipHeight = static_cast<int32_t>(tileSize);

  for (uint32_t i = 1; i < mipLevelCount; ++i) {
    // mip i-1 was eGeneral (i==1, per the invariant above) or
    // eTransferDstOptimal (i>1, written by the previous iteration's blit)
    // - either way, move it to eTransferSrcOptimal so this iteration can
    // blit FROM it.
    vk::ImageLayout prevMipPriorLayout =
        (i == 1) ? vk::ImageLayout::eGeneral
                 : vk::ImageLayout::eTransferDstOptimal;
    displacementFoamMap->recordTransitionMipLayout(
        cmd, prevMipPriorLayout, vk::ImageLayout::eTransferSrcOptimal, i - 1,
        1);
    // mip i is eGeneral (never touched yet this frame) - move it to
    // eTransferDstOptimal so this iteration can blit INTO it.
    displacementFoamMap->recordTransitionMipLayout(
        cmd, vk::ImageLayout::eGeneral, vk::ImageLayout::eTransferDstOptimal, i,
        1);

    vk::ImageBlit blit{};
    blit.srcOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.srcOffsets[1] = vk::Offset3D{mipWidth, mipHeight, 1};
    blit.srcSubresource = vk::ImageSubresourceLayers{
        vk::ImageAspectFlagBits::eColor, i - 1, 0, 1};

    int32_t nextMipWidth = mipWidth > 1 ? mipWidth / 2 : 1;
    int32_t nextMipHeight = mipHeight > 1 ? mipHeight / 2 : 1;
    blit.dstOffsets[0] = vk::Offset3D{0, 0, 0};
    blit.dstOffsets[1] = vk::Offset3D{nextMipWidth, nextMipHeight, 1};
    blit.dstSubresource =
        vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, i, 0, 1};

    cmd.blitImage(static_cast<vk::Image>(*displacementFoamMap),
                  vk::ImageLayout::eTransferSrcOptimal,
                  static_cast<vk::Image>(*displacementFoamMap),
                  vk::ImageLayout::eTransferDstOptimal, 1, &blit,
                  vk::Filter::eLinear);

    mipWidth = nextMipWidth;
    mipHeight = nextMipHeight;
  }

  // Every level is now diverged: mip 0..mipLevelCount-2 sit in
  // eTransferSrcOptimal (each was blitted FROM), and the last mip sits in
  // eTransferDstOptimal (only ever blitted INTO, never read from - there's
  // no mip beyond it). Bring every level to eShaderReadOnlyOptimal with
  // two explicit-oldLayout barriers (one per group, since each group's
  // real prior layout differs), then resync the tracked `layout` member
  // via setTrackedLayoutUnsafe() - NOT another recordTransitionLayout()
  // call, which would incorrectly build a barrier from the stale tracked
  // value (eGeneral) even though every subresource has already physically
  // reached eShaderReadOnlyOptimal below.
  displacementFoamMap->recordTransitionMipLayout(
      cmd, vk::ImageLayout::eTransferSrcOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal, 0, mipLevelCount - 1);
  displacementFoamMap->recordTransitionMipLayout(
      cmd, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageLayout::eShaderReadOnlyOptimal, mipLevelCount - 1, 1);

  // Whole resource is now uniformly eShaderReadOnlyOptimal - restore the
  // invariant the top of this function (and the top of the next frame's
  // post-FFT block) depends on, without issuing a bogus barrier.
  displacementFoamMap->setTrackedLayoutUnsafe(
      vk::ImageLayout::eShaderReadOnlyOptimal);

  debug::endLabel(context, cmd);
}

void WSTessendorf::warmUp() {
  auto cmd = beginSingleTimeCommands(context);
  // Compute-only work (the pre/post FFT pipelines and VkFFT itself, which
  // was configured with context.vulkan.computeQueue in this class's
  // constructor) must be submitted to the compute queue, matching every
  // other place this command sequence runs from - see
  // recordComputeWaves()'s own callers.
  recordComputeWaves(cmd, 0.f);
  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);
}

} // namespace vkh
