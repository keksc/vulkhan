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
      context, {// Binding 0: Storage Image (Output)
                vk::DescriptorSetLayoutBinding{
                    0, vk::DescriptorType::eStorageImage, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr},
                // Binding 1: Color Sampler (Input)
                vk::DescriptorSetLayoutBinding{
                    1, vk::DescriptorType::eCombinedImageSampler, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr},
                // Binding 2: Depth Sampler (Input)
                vk::DescriptorSetLayoutBinding{
                    2, vk::DescriptorType::eCombinedImageSampler, 1,
                    vk::ShaderStageFlagBits::eCompute, nullptr}});

  debug::setObjName(
      context, vk::ObjectType::eDescriptorSetLayout,
      reinterpret_cast<uint64_t>(static_cast<VkDescriptorSetLayout>(setLayout)),
      "post processing set layout");

  auto imageCount = context.vulkan.swapChain->imageCount();
  descriptorSets.reserve(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    auto &set = descriptorSets.emplace_back();
    set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

    vk::DescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getImageView(i);
    swapImageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageView = context.vulkan.swapChain->getDepthImageView(i);
    depthImageInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    depthImageInfo.sampler = context.vulkan.defaultSampler;

    DescriptorWriter writer(context);
    writer.writeImage(0, swapImageInfo, vk::DescriptorType::eStorageImage);
    writer.writeImage(1, depthImageInfo,
                      vk::DescriptorType::eCombinedImageSampler);
    writer.updateSet(descriptorSets[i]);
  }
}

void PostProcessingSys::recreateDescriptors() {
  descriptorSets.clear();

  auto imageCount = context.vulkan.swapChain->imageCount();
  descriptorSets.reserve(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    auto &set = descriptorSets.emplace_back();
    set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

    vk::DescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getImageView(i);
    swapImageInfo.imageLayout = vk::ImageLayout::eGeneral;

    vk::DescriptorImageInfo colorInfo{};
    colorInfo.imageView = context.vulkan.swapChain->getDepthImageView(i);
    colorInfo.imageLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
    colorInfo.sampler = context.vulkan.defaultSampler;

    vk::DescriptorImageInfo depthInfo{
        context.vulkan.defaultSampler,
        context.vulkan.swapChain->getDepthImageView(i),
        vk::ImageLayout::eShaderReadOnlyOptimal};

    DescriptorWriter writer(context);
    writer.writeImage(0, swapImageInfo, vk::DescriptorType::eStorageImage);
    writer.writeImage(1, colorInfo, vk::DescriptorType::eStorageImage);
    writer.writeImage(2, depthInfo, vk::DescriptorType::eStorageImage);
    writer.updateSet(descriptorSets[i]);
  }
}

void PostProcessingSys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.pSetLayouts = setLayouts.data();
  layoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());

  pipeline = std::make_unique<ComputePipeline>(
      context, "shaders/postProcessing.comp.spv", layoutInfo,
      "post processing");
}

PostProcessingSys::PostProcessingSys(EngineContext &context) : System(context) {
  createDescriptors();
  createPipeline();
  savedSwapChain = context.vulkan.swapChain.get();
}

void PostProcessingSys::run(vk::CommandBuffer cmd, uint32_t imageIndex) {
  if (context.vulkan.swapChain.get() != savedSwapChain) {
    savedSwapChain = context.vulkan.swapChain.get();
    recreateDescriptors();
  }

  vk::ImageMemoryBarrier barriers[2]{};

  // Swap chain image: UNDEFINED -> GENERAL
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
  barriers[1].image = context.vulkan.swapChain->getDepthImage(imageIndex);
  barriers[1].subresourceRange.aspectMask = vk::ImageAspectFlagBits::eDepth;
  barriers[1].subresourceRange.baseMipLevel = 0;
  barriers[1].subresourceRange.levelCount = 1;
  barriers[1].subresourceRange.baseArrayLayer = 0;
  barriers[1].subresourceRange.layerCount = 1;
  barriers[1].srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  barriers[1].dstAccessMask = vk::AccessFlagBits::eShaderRead;

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe |
                          vk::PipelineStageFlagBits::eEarlyFragmentTests,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr, 2,
                      barriers);

  // Bind pipeline
  pipeline->bind(cmd);

  // Bind descriptor sets
  std::vector<vk::DescriptorSet> sets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      descriptorSets[imageIndex]};
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, pipeline->getLayout(),
                         0, static_cast<uint32_t>(sets.size()), sets.data(), 0,
                         nullptr);

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
