#include "godRaysEffect.hpp"
#include "../../../settings.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>

#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../engineContext.hpp"
#include "../../image.hpp"
#include "../../swapChain.hpp"

namespace vkh {

GodRaysEffect::GodRaysEffect(EngineContext &context) : context(context) {
  const char *cache = std::getenv("VULKHAN_GODRAYS_CACHE");
  useSkyMask = !cache || cache[0] != '0';
  // Linear + clamp: raysImages is half (or quarter) the resolution of
  // everything else in the chain, so unlike PostProcessingSys's
  // same-resolution nearest hand-off sampler, upsampling THIS actually
  // needs real bilinear filtering - see this member's header comment.
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;
  raysSampler = context.vulkan.device.createSampler(samplerInfo);
  debug::setObjName(
      context, vk::ObjectType::eSampler,
      reinterpret_cast<uint64_t>(static_cast<VkSampler>(raysSampler)),
      "god rays half-res upsample sampler");

  // Own descriptor set layout for the generate pass - entirely separate
  // from the one ComputePass builds for this effect's main (composite)
  // dispatch, since this runs on its own pipeline via beforeDispatch(),
  // not through ComputePass's generic machinery. Binding numbers here
  // start at 0, not kFixedBindingCount, precisely because this ISN'T one
  // of the fixed-binding postprocessing shaders - see godraysGenerate.
  // comp's header comment.
  std::vector<vk::DescriptorSetLayoutBinding> generateBindings = {
      // binding 0: half-res storage image (output)
      {0, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      // binding 1: full-res resolved scene color (input)
      {1, vk::DescriptorType::eCombinedImageSampler, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      // binding 2: full-res resolved scene depth (input)
      {2, vk::DescriptorType::eCombinedImageSampler, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {3, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {4, vk::DescriptorType::eCombinedImageSampler, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
  };
  generateSetLayout = buildDescriptorSetLayout(context, generateBindings);
  debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkDescriptorSetLayout>(generateSetLayout)),
                    "black hole god rays (generate)");

  createGeneratePipeline();
  recreateRaysImages(
      static_cast<uint32_t>(context.vulkan.swapChain->imageCount()));
}

GodRaysEffect::~GodRaysEffect() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(generateSetLayout);
    context.vulkan.device.destroySampler(raysSampler);
  }
}

void GodRaysEffect::createGeneratePipeline() {
  // Set 0 = the same global descriptor set layout every other pipeline in
  // this engine binds set 0 to (globalUbo.glsl's ubo.projView/ubo.time,
  // which godraysGenerate.comp reads directly) - required here because
  // this is a genuinely separate pipeline with its own layout, not
  // reusing ComputePass's; binding the global set to it in
  // beforeDispatch() below only works if this layout actually declares
  // it at set 0. Set 1 = generateSetLayout, built above.
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, generateSetLayout};

  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  pcRange.offset = 0;
  pcRange.size = sizeof(Push);

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.pSetLayouts = setLayouts.data();
  layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pcRange;

  generatePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/postprocessing/godraysGenerate.comp.spv", layoutInfo,
      "black hole god rays (generate, half-res)");
  maskPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/postprocessing/godraysMask.comp.spv", layoutInfo,
      "god rays sky visibility");
}

void GodRaysEffect::recreateRaysImages(uint32_t imageCount) {
  // Start from context.window.size (whatever concrete type that is - see
  // PostProcessingSys::makeIntermediateImages, which assigns it straight
  // into an ImageCreateInfo_empty::size the same way) so this compiles
  // regardless of that type's exact shape, then overwrite just the
  // components with the halved (or quartered) values - avoids needing to
  // name that type explicitly here.
  raysWidth_ =
      std::max<uint32_t>(1u, context.window.size.x / kResolutionDivisor);
  raysHeight_ =
      std::max<uint32_t>(1u, context.window.size.y / kResolutionDivisor);

  ImageCreateInfo_empty createInfo{};
  createInfo.size = context.window.size;
  createInfo.size.x = raysWidth_;
  createInfo.size.y = raysHeight_;
  // Floating-point, not rgba8 - this holds a not-yet-tonemapped additive
  // light contribution that godrays.comp still has to scale by
  // tint/intensity and add to the base scene color; clamping it into an
  // 8-bit UNORM HERE, a frame before that final combine, would clip
  // bright rays earlier (and differently) than the original single-pass
  // version ever did. See godrays.comp's upsample comment.
  createInfo.format = vk::Format::eR16G16B16A16Sfloat;
  createInfo.usage =
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.name = "god rays half-res accumulation";

  raysImages.clear();
  raysImages.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; i++)
    raysImages.push_back(std::make_shared<Image2D>(context, createInfo));

  createInfo.size = context.window.size;
  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "god rays sky visibility";
  skyMasks.clear();
  skyMasks.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; ++i)
    skyMasks.push_back(std::make_shared<Image2D>(context, createInfo));

  generateDescriptorSets.clear();
  generateDescriptorSets.reserve(imageCount);
  for (uint32_t i = 0; i < imageCount; i++) {
    vk::DescriptorSet set =
        context.vulkan.globalDescriptorAllocator->allocate(generateSetLayout);

    // Sampler is irrelevant for a storage-image binding (ignored by
    // imageStore) - passing raysSampler here is just convenient, not
    // meaningful.
    VkDescriptorImageInfo rawOutput =
        raysImages[i]->getDescriptorInfo(raysSampler);
    vk::DescriptorImageInfo outputInfo{};
    outputInfo.imageView = rawOutput.imageView;
    outputInfo.imageLayout = vk::ImageLayout::eGeneral;

    // Always the TRUE resolved scene color/depth for THIS swapchain
    // image, regardless of chain position - see beforeDispatch()'s doc
    // comment on why that's correct here even though the outer composite
    // dispatch's binding 1 does respect chaining.
    vk::DescriptorImageInfo sceneColorInfo{};
    sceneColorInfo.sampler = context.vulkan.defaultSampler;
    sceneColorInfo.imageView = context.vulkan.swapChain->getSceneColorView(i);
    sceneColorInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::DescriptorImageInfo depthInfo{};
    depthInfo.sampler = context.vulkan.defaultSampler;
    depthInfo.imageView =
        context.vulkan.swapChain->getResolvedDepthImageView(i);
    depthInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

    DescriptorWriter writer(context);
    writer.writeImage(0, outputInfo, vk::DescriptorType::eStorageImage);
    writer.writeImage(1, sceneColorInfo,
                      vk::DescriptorType::eCombinedImageSampler);
    writer.writeImage(2, depthInfo, vk::DescriptorType::eCombinedImageSampler);
    vk::DescriptorImageInfo maskInfo{};
    maskInfo.imageView = skyMasks[i]->getDescriptorInfo(raysSampler).imageView;
    maskInfo.imageLayout = vk::ImageLayout::eGeneral;
    writer.writeImage(3, maskInfo, vk::DescriptorType::eStorageImage);
    maskInfo.sampler = raysSampler;
    writer.writeImage(4, maskInfo, vk::DescriptorType::eCombinedImageSampler);
    writer.updateSet(set);

    generateDescriptorSets.push_back(set);
  }
}

void GodRaysEffect::onImageCountChanged(uint32_t imageCount) {
  recreateRaysImages(imageCount);
}

std::vector<vk::DescriptorSetLayoutBinding>
GodRaysEffect::additionalBindings() const {
  return {
      // binding kFixedBindingCount (3): the half-res ray image, sampled
      // (with bilinear filtering - see raysSampler) by godrays.comp.
      {kFixedBindingCount, vk::DescriptorType::eCombinedImageSampler, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
  };
}

void GodRaysEffect::writeAdditionalDescriptors(DescriptorWriter &writer,
                                               uint32_t imageIndex) {
  VkDescriptorImageInfo raw =
      raysImages[imageIndex]->getDescriptorInfo(raysSampler);
  vk::DescriptorImageInfo info{};
  info.sampler = raw.sampler;
  info.imageView = raw.imageView;
  info.imageLayout = vk::ImageLayout::eGeneral;
  writer.writeImage(kFixedBindingCount, info,
                    vk::DescriptorType::eCombinedImageSampler);
}

void GodRaysEffect::beforeDispatch(vk::CommandBuffer cmd, uint32_t imageIndex) {
  // Runs BEFORE ComputePass<Push>'s own fixed barriers/dispatch (see this
  // hook's doc comment in computePass.hpp) - this is where the actual
  // expensive 48-sample radial march (godraysGenerate.comp) happens now,
  // at HALF resolution (kResolutionDivisor), into raysImages[imageIndex].
  // The dispatch ComputePass::run() drives right after this returns -
  // godrays.comp itself - is now just a cheap bilinear upsample +
  // additive blend at full resolution. This split is what took this
  // effect from ~40ms/frame (measured via RenderDoc at native 1920x1200,
  // 48 samples/pixel, EID 702 in that capture) down to a fraction of
  // that: the expensive part now only runs over
  // 1/(kResolutionDivisor^2) as many pixels.
  //
  // NOTE: unlike the outer dispatch's binding 1 (which respects
  // PostProcessingSys chaining - see InputTargetProvider in
  // computePass.hpp), generateDescriptorSets always points at the TRUE
  // resolved scene color/depth (see recreateRaysImages() above),
  // regardless of this effect's position in a chain. That's intentional,
  // not an oversight: the ray march is measuring where the black hole's
  // OWN light actually is in the real scene, not compositing over
  // whatever an earlier chain stage already did - only the final blend
  // (godrays.comp) needs to respect chain position for the BASE color it
  // adds rays on top of, and it still does.
  vk::ImageMemoryBarrier toWrite{};
  toWrite.oldLayout = vk::ImageLayout::eUndefined;
  toWrite.newLayout = vk::ImageLayout::eGeneral;
  toWrite.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toWrite.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toWrite.image = static_cast<vk::Image>(*raysImages[imageIndex]);
  toWrite.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  toWrite.srcAccessMask = vk::AccessFlags{};
  toWrite.dstAccessMask = vk::AccessFlagBits::eShaderWrite;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                      &toWrite);

  if (useSkyMask) {
    auto maskBarrier = toWrite;
    maskBarrier.image = static_cast<vk::Image>(*skyMasks[imageIndex]);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, {}, 0,
                        nullptr, 0, nullptr, 1, &maskBarrier);
    maskPipeline->bind(cmd);
    std::array<vk::DescriptorSet, 2> maskSets{
        context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
        generateDescriptorSets[imageIndex]};
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           maskPipeline->getLayout(), 0, 2, maskSets.data(), 0,
                           nullptr);
    const auto extent = context.vulkan.swapChain->getSwapChainExtent();
    cmd.dispatch((extent.width + 15) / 16, (extent.height + 15) / 16, 1);
    maskBarrier.oldLayout = vk::ImageLayout::eGeneral;
    maskBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    maskBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, {}, 0,
                        nullptr, 0, nullptr, 1, &maskBarrier);
  }

  generatePipeline->bind(cmd);

  std::vector<vk::DescriptorSet> sets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      generateDescriptorSets[imageIndex]};
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, generatePipeline->getLayout(), 0,
      static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

  // Same push constants makePushConstants() builds for the outer
  // composite dispatch - godraysGenerate.comp just uses a different
  // subset of the fields (rayParams/maskParams, not tintAndIntensity -
  // see that shader's own push-constant comment for why tint/intensity
  // are applied once, in the composite, rather than here).
  Push push = makePushConstants();
  cmd.pushConstants(generatePipeline->getLayout(),
                    vk::ShaderStageFlagBits::eCompute, 0, sizeof(Push), &push);

  uint32_t groupCountX = (raysWidth_ + 15) / 16;
  uint32_t groupCountY = (raysHeight_ + 15) / 16;
  cmd.dispatch(groupCountX, groupCountY, 1);

  // Stays GENERAL, just flip write -> read so the immediately-following
  // main dispatch (godrays.comp, same command buffer) can safely sample
  // it - same pattern ComputePass itself uses for a chained
  // intermediate's hand-off barrier.
  vk::ImageMemoryBarrier toRead{};
  toRead.oldLayout = vk::ImageLayout::eGeneral;
  toRead.newLayout = vk::ImageLayout::eGeneral;
  toRead.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toRead.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  toRead.image = static_cast<vk::Image>(*raysImages[imageIndex]);
  toRead.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  toRead.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                      &toRead);
}

GodRaysEffect::Push GodRaysEffect::makePushConstants() const {
  Push push{};
  // Where the black hole is, and where it lands on screen, are both
  // computed inside godraysGenerate.comp itself (see
  // lightScreenPosAndVisibility() there) - this only forwards the visual
  // tuning knobs. Used for BOTH dispatches (see beforeDispatch()) - each
  // shader just reads the subset of fields relevant to it.
  push.tintAndIntensity = glm::vec4(
      tint, intensity * float(settings::current().lightShaftStrength) / 100.f);
  push.rayParams = glm::vec4(density, decay, weight, exposure);
  push.maskParams = glm::vec4(diskLuminanceThreshold, starLuminanceThreshold,
                              starWeight, useSkyMask ? 1.f : 0.f);
  return push;
}

} // namespace vkh
