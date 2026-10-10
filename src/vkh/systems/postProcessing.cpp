#include "postProcessing.hpp"

#include <cassert>

#include "../buffer.hpp"
#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../engineContext.hpp"
#include "../image.hpp"
#include "../swapChain.hpp"

namespace vkh {

vk::Sampler PostProcessingSys::makeIntermediateSampler(EngineContext &context) {
  // Nearest/clamp: each hand-off image is sampled exactly once per output
  // pixel at matching resolution by the next stage in the chain, never
  // magnified, minified, or tiled - linear filtering and repeat addressing
  // would just be wasted work here, unlike e.g. WaterSys's repeatSampler
  // which genuinely needs both.
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eNearest;
  samplerInfo.minFilter = vk::Filter::eNearest;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eClampToEdge;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eClampToEdge;

  vk::Sampler sampler = context.vulkan.device.createSampler(samplerInfo);
  debug::setObjName(context, vk::ObjectType::eSampler,
                    reinterpret_cast<uint64_t>(static_cast<VkSampler>(sampler)),
                    "postprocessing chain hand-off sampler");
  return sampler;
}

std::vector<Image2D>
PostProcessingSys::makeIntermediateImages(EngineContext &context,
                                          uint32_t imageCount) {
  std::vector<Image2D> images;
  images.reserve(imageCount);

  ImageCreateInfo_empty createInfo{};
  createInfo.size = context.window.size;
  // rgba8: matches the resolved scene color's own bit depth (see
  // SwapChain::createSceneColorResources) - no precision is gained by
  // widening a hand-off that's just relaying one stage's finished output
  // to the next.
  createInfo.format = vk::Format::eR8G8B8A8Unorm;
  createInfo.usage =
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
  createInfo.layout = vk::ImageLayout::eGeneral;

  for (uint32_t i = 0; i < imageCount; i++) {
    createInfo.name = "postprocessing chain hand-off image";
    images.emplace_back(context, createInfo);
  }
  return images;
}

std::vector<vk::DescriptorImageInfo>
PostProcessingSys::makeDescInfos(std::vector<Image2D> &images,
                                 vk::Sampler sampler) {
  std::vector<vk::DescriptorImageInfo> infos;
  infos.reserve(images.size());
  for (auto &image : images) {
    // Image2D::getDescriptorInfo() returns a raw VkDescriptorImageInfo
    // pointing at whatever layout the image was created with; every
    // stage always reads/writes these as eGeneral (matches how
    // ComputePass's chaining barrier leaves them - see computePass.hpp),
    // so the layout is pinned explicitly here rather than trusted from
    // the image.
    VkDescriptorImageInfo raw = image.getDescriptorInfo(sampler);
    vk::DescriptorImageInfo info{};
    info.sampler = raw.sampler;
    info.imageView = raw.imageView;
    info.imageLayout = vk::ImageLayout::eGeneral;
    infos.push_back(info);
  }
  return infos;
}

PostProcessingSys::PostProcessingSys(
    EngineContext &context, std::vector<ComputePassFactory> effectFactories)
    : System(context), intermediateSampler{makeIntermediateSampler(context)} {
  buildChain(effectFactories);
}

PostProcessingSys::~PostProcessingSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroySampler(intermediateSampler, nullptr);
  }
}

void PostProcessingSys::buildChain(
    std::vector<ComputePassFactory> &effectFactories) {
  // A chain needs at least one stage to own the real swapchain image and
  // its present-layout transition (see run()/runPassthrough() above) -
  // there's no meaningful "chain of zero effects" for this class to run.
  assert(!effectFactories.empty() &&
         "PostProcessingSys needs at least one effect");

  const size_t stageCount = effectFactories.size();
  const uint32_t imageCount =
      static_cast<uint32_t>(context.vulkan.swapChain->imageCount());

  // stageCount - 1 hand-off boundaries between stages; none for a
  // single-stage (or empty) chain.
  const size_t boundaryCount = stageCount > 0 ? stageCount - 1 : 0;
  intermediateImages.reserve(boundaryCount);
  intermediateDescInfos.reserve(boundaryCount);
  for (size_t b = 0; b < boundaryCount; ++b) {
    intermediateImages.push_back(makeIntermediateImages(context, imageCount));
    intermediateDescInfos.push_back(
        makeDescInfos(intermediateImages.back(), intermediateSampler));
  }

  chain.reserve(stageCount);
  for (size_t i = 0; i < stageCount; ++i) {
    const bool isLast = (i + 1 == stageCount);
    // Every stage but the first already has depth/scene color left
    // shader-read-only by the stage before it this same frame (see
    // ComputePass's skipInputTransitions doc comment).
    const bool skipInputTransitions = (i != 0);

    OutputTargetProvider outputTarget = nullptr;
    if (!isLast) {
      // Capture `this` and the boundary index, not the vector/image
      // directly, so a later resizeIntermediateImages() call (swapchain
      // recreation) is picked up automatically without reconstructing
      // this stage.
      size_t boundary = i;
      outputTarget =
          [this, boundary](uint32_t imageIndex) -> ComputePassOutputTarget {
        auto &images = intermediateImages[boundary];
        auto &infos = intermediateDescInfos[boundary];
        return {static_cast<vk::Image>(images[imageIndex]),
                infos[imageIndex].imageView};
      };
    }

    // Every stage but the first reads the previous stage's hand-off image
    // (boundary i-1) as its binding-1 input, instead of the default real
    // resolved scene color - otherwise each stage would independently
    // reprocess the original scene and only the last stage's result would
    // ever be visible. Same capture-by-index reasoning as outputTarget
    // above, so a later resize is picked up automatically.
    InputTargetProvider inputTarget = nullptr;
    if (i != 0) {
      size_t prevBoundary = i - 1;
      inputTarget =
          [this, prevBoundary](uint32_t imageIndex) -> vk::DescriptorImageInfo {
        return intermediateDescInfos[prevBoundary][imageIndex];
      };
    }

    chain.push_back(effectFactories[i](context, outputTarget, inputTarget,
                                       skipInputTransitions));
  }
}

void PostProcessingSys::resizeIntermediateImages(uint32_t imageCount) {
  // Destroys the old Image2D objects (and their views/memory) as each
  // assignment replaces that boundary's vector contents, then rebuilds
  // descriptor infos against the new images. No stage has had
  // recreateDescriptors() called yet at this point (see
  // PostProcessingSys::recreateDescriptors() below), so nothing is still
  // reading the old images' descriptors when they're destroyed here.
  for (size_t b = 0; b < intermediateImages.size(); ++b) {
    intermediateImages[b] = makeIntermediateImages(context, imageCount);
    intermediateDescInfos[b] =
        makeDescInfos(intermediateImages[b], intermediateSampler);
  }
}

void PostProcessingSys::recreateDescriptors() {
  resizeIntermediateImages(
      static_cast<uint32_t>(context.vulkan.swapChain->imageCount()));

  // Order matters: an earlier stage's descriptor sets must be rebuilt
  // before a later stage's - each outputTarget lambda just indexes into
  // intermediateImages/intermediateDescInfos (already resized above), and
  // a later stage reads the same boundary's descriptor infos as an input
  // binding, so it needs those already pointing at the new images.
  for (auto &pass : chain)
    pass->recreateDescriptors();
}

void PostProcessingSys::run(uint32_t imageIndex) {
  // PostProcessingSys requires at least one stage (see buildChain()'s
  // assert) - an empty chain has no pass that owns the present-layout
  // transition, so there'd be nothing to fall back to here either.
  if (!enabled) {
    // Bypasses the whole chain - see ComputePass::runPassthrough's doc
    // comment on why this always targets the real swapchain image
    // regardless of which pass instance it's called on.
    chain.back()->runPassthrough(imageIndex);
    return;
  }

  // Each stage's run() already leaves its hand-off image in eGeneral with
  // a shader-write -> shader-read barrier applied (see computePass.hpp's
  // `if (outputTarget)` branch) before returning, so the next stage can
  // safely sample it here with no extra barrier needed.
  for (auto &pass : chain)
    pass->run(imageIndex);
}

} // namespace vkh
