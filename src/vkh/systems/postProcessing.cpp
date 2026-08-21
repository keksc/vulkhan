#include "postProcessing.hpp"

#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan.hpp>

#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

namespace vkh {

void PostProcessingSys::createDescriptors() {
  setLayout = buildDescriptorSetLayout(
      context, {// Binding 0: Storage Image (Output) — swapchain image
                vk::DescriptorSetLayoutBinding{
                    0, vk::DescriptorType::eStorageImage, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr},
                // Binding 1: Scene Color Sampler (Input) — resolved 1x color
                vk::DescriptorSetLayoutBinding{
                    1, vk::DescriptorType::eCombinedImageSampler, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr},
                // Binding 2: Depth Sampler (Input)
                vk::DescriptorSetLayoutBinding{
                    2, vk::DescriptorType::eCombinedImageSampler, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr},
                // Binding 3: Smoke dye volume (Input) — sampler3D, periodic
                vk::DescriptorSetLayoutBinding{
                    3, vk::DescriptorType::eCombinedImageSampler, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr}});

  debug::setObjName(
      context, vk::ObjectType::eDescriptorSetLayout,
      reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(setLayout)),
      "post processing set layout");

  allocateAndWriteDescriptorSets();
}

void PostProcessingSys::recreateDescriptors() {
  descriptorSets.clear();
  allocateAndWriteDescriptorSets();
}

// Shared by createDescriptors() and recreateDescriptors() so the binding
// layout only has to be kept in sync with createDescriptors()'s
// buildDescriptorSetLayout() call in one place.
void PostProcessingSys::allocateAndWriteDescriptorSets() {
  auto imageCount = context.vulkan.swapChain->imageCount();
  descriptorSets.reserve(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    auto &set = descriptorSets.emplace_back();
    set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

    vk::DescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getStorageImageView(i);
    swapImageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo sceneColorInfo{};
    sceneColorInfo.sampler = context.vulkan.defaultSampler;
    sceneColorInfo.imageView = context.vulkan.swapChain->getSceneColorView(i);
    sceneColorInfo.imageLayout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::DescriptorImageInfo depthImageInfo{};
    depthImageInfo.sampler = context.vulkan.defaultSampler;
    depthImageInfo.imageView =
        context.vulkan.swapChain->getResolvedDepthImageView(i);
    depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;

    // The dye volume isn't per-swapchain-image (it's owned by FluidGrid,
    // one shared image), so this same descriptor info is written into
    // every per-image set below - not indexed by i like the three above.
    vk::DescriptorImageInfo smokeVolumeInfo =
        smokeGrid.dyeImage->getDescriptorInfo(smokeVolumeSampler);

    DescriptorWriter writer(context);
    writer.writeImage(0, swapImageInfo, vk::DescriptorType::eStorageImage);
    writer.writeImage(1, sceneColorInfo,
                      vk::DescriptorType::eCombinedImageSampler);
    writer.writeImage(2, depthImageInfo,
                      vk::DescriptorType::eCombinedImageSampler);
    writer.writeImage(3, smokeVolumeInfo,
                      vk::DescriptorType::eCombinedImageSampler);
    writer.updateSet(descriptorSets[i]);
  }
}

void PostProcessingSys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  pcRange.offset = 0;
  pcRange.size = sizeof(SmokePush);

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.pSetLayouts = setLayouts.data();
  layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pcRange;

  pipeline = std::make_unique<ComputePipeline>(
      context, "shaders/postProcessing.comp.spv", layoutInfo,
      "post processing");
}

PostProcessingSys::PostProcessingSys(EngineContext &context,
                                     FluidGrid &smokeGrid)
    : System(context), smokeGrid{smokeGrid} {
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
  samplerInfo.unnormalizedCoordinates = false;
  if (context.vulkan.device.createSampler(&samplerInfo, nullptr,
                                          &smokeVolumeSampler) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create smoke volume sampler!");

  createDescriptors();
  createPipeline();
  savedSwapChain = context.vulkan.swapChain.get();
}

PostProcessingSys::~PostProcessingSys() {
  context.vulkan.device.destroyDescriptorSetLayout(setLayout);
  context.vulkan.device.destroySampler(smokeVolumeSampler);
}

void PostProcessingSys::runPassthrough(uint32_t imageIndex) {
  auto &cmd = context.frameInfo.cmd;

  // No compute pass this frame: just copy the resolved scene color directly
  // into the swapchain image and get it into a presentable layout. Scene
  // color and the swapchain image share format and extent (see
  // SwapChain::createSceneColorResources), so a straight copy works — no
  // blit/filtering needed.
  //
  // NOTE: smoke compositing lives in the compute path below (run()'s
  // dispatch), not here - smoke will not render at all while postprocessing
  // is disabled. If that's ever a problem, this is where a smoke-only
  // fallback composite would need to go.

  vk::ImageMemoryBarrier barriers[2]{};

  // Scene color: COLOR_ATTACHMENT_OPTIMAL -> TRANSFER_SRC_OPTIMAL
  barriers[0].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
  barriers[0].newLayout = vk::ImageLayout::eTransferSrcOptimal;
  barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].image = context.vulkan.swapChain->getSceneColorImage(imageIndex);
  barriers[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barriers[0].subresourceRange.baseMipLevel = 0;
  barriers[0].subresourceRange.levelCount = 1;
  barriers[0].subresourceRange.baseArrayLayer = 0;
  barriers[0].subresourceRange.layerCount = 1;
  barriers[0].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  barriers[0].dstAccessMask = vk::AccessFlagBits::eTransferRead;

  // Swapchain image: UNDEFINED -> TRANSFER_DST_OPTIMAL
  barriers[1].oldLayout = vk::ImageLayout::eUndefined;
  barriers[1].newLayout = vk::ImageLayout::eTransferDstOptimal;
  barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].image = context.vulkan.swapChain->getImage(imageIndex);
  barriers[1].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barriers[1].subresourceRange.baseMipLevel = 0;
  barriers[1].subresourceRange.levelCount = 1;
  barriers[1].subresourceRange.baseArrayLayer = 0;
  barriers[1].subresourceRange.layerCount = 1;
  barriers[1].srcAccessMask = vk::AccessFlags{};
  barriers[1].dstAccessMask = vk::AccessFlagBits::eTransferWrite;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eColorAttachmentOutput,
                      vk::PipelineStageFlagBits::eTransfer,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 2,
                      barriers);

  auto extent = context.vulkan.swapChain->getSwapChainExtent();

  vk::ImageCopy region{};
  region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
  region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
  region.extent = vk::Extent3D{extent.width, extent.height, 1};

  cmd.copyImage(context.vulkan.swapChain->getSceneColorImage(imageIndex),
                vk::ImageLayout::eTransferSrcOptimal,
                context.vulkan.swapChain->getImage(imageIndex),
                vk::ImageLayout::eTransferDstOptimal, 1, &region);

  // Swapchain image: TRANSFER_DST_OPTIMAL -> PRESENT_SRC_KHR
  barriers[1].oldLayout = vk::ImageLayout::eTransferDstOptimal;
  barriers[1].newLayout = vk::ImageLayout::ePresentSrcKHR;
  barriers[1].srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  barriers[1].dstAccessMask = vk::AccessFlagBits::eMemoryRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eBottomOfPipe,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                      &barriers[1]);
}

void PostProcessingSys::run(uint32_t imageIndex) {
  auto &cmd = context.frameInfo.cmd;
  if (context.vulkan.swapChain.get() != savedSwapChain) {
    savedSwapChain = context.vulkan.swapChain.get();
    recreateDescriptors();
  }

  if (!enabled) {
    runPassthrough(imageIndex);
    return;
  }

  vk::ImageMemoryBarrier barriers[3]{};

  // Swap chain image: UNDEFINED -> GENERAL (output)
  barriers[0].oldLayout = vk::ImageLayout::eUndefined;
  barriers[0].newLayout = vk::ImageLayout::eGeneral;
  barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].image = context.vulkan.swapChain->getImage(imageIndex);
  barriers[0].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barriers[0].subresourceRange.baseMipLevel = 0;
  barriers[0].subresourceRange.levelCount = 1;
  barriers[0].subresourceRange.baseArrayLayer = 0;
  barriers[0].subresourceRange.layerCount = 1;
  barriers[0].srcAccessMask = vk::AccessFlags{};
  barriers[0].dstAccessMask =
      vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite;

  // Depth image: DEPTH_STENCIL_ATTACHMENT_OPTIMAL ->
  // DEPTH_STENCIL_READ_ONLY_OPTIMAL
  barriers[1].oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  barriers[1].newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
  barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].image =
      context.vulkan.swapChain->getResolvedDepthImage(imageIndex);
  barriers[1].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
  barriers[1].subresourceRange.baseMipLevel = 0;
  barriers[1].subresourceRange.levelCount = 1;
  barriers[1].subresourceRange.baseArrayLayer = 0;
  barriers[1].subresourceRange.layerCount = 1;
  barriers[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

  // Scene color: COLOR_ATTACHMENT_OPTIMAL -> SHADER_READ_ONLY_OPTIMAL (input)
  barriers[2].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
  barriers[2].newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  barriers[2].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[2].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[2].image = context.vulkan.swapChain->getSceneColorImage(imageIndex);
  barriers[2].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
  barriers[2].subresourceRange.baseMipLevel = 0;
  barriers[2].subresourceRange.levelCount = 1;
  barriers[2].subresourceRange.baseArrayLayer = 0;
  barriers[2].subresourceRange.layerCount = 1;
  barriers[2].srcAccessMask = vk::AccessFlagBits::eColorAttachmentWrite;
  barriers[2].dstAccessMask = vk::AccessFlagBits::eShaderRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe |
                          vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eColorAttachmentOutput,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 3,
                      barriers);

  pipeline->bind(cmd);

  std::vector<vk::DescriptorSet> sets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      descriptorSets[imageIndex]};
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->getLayout(),
                         0, static_cast<uint32_t>(sets.size()), sets.data(), 0,
                         nullptr);

  SmokePush push{};
  push.boxMin =
      glm::vec4(glm::vec3(smokeGrid.gridOrigin) * smokeGrid.cellSize,
               smokeGrid.cellSize);
  push.cellCount = glm::ivec4(smokeGrid.cellCount, 0);
  cmd.pushConstants(pipeline->getLayout(), vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(SmokePush), &push);

  // Dispatch compute shader
  uint32_t groupCountX = (context.window.size.x + 15) / 16;
  uint32_t groupCountY = (context.window.size.y + 15) / 16;
  cmd.dispatch(groupCountX, groupCountY, 1);

  barriers[0].oldLayout = vk::ImageLayout::eGeneral;
  barriers[0].newLayout = vk::ImageLayout::ePresentSrcKHR;
  barriers[0].srcAccessMask = vk::AccessFlagBits::eShaderWrite;
  barriers[0].dstAccessMask = vk::AccessFlagBits::eMemoryRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eBottomOfPipe,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 1,
                      &barriers[0]);
}

} // namespace vkh
