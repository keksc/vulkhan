#include "postProcessing.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

namespace vkh {
void PostProcessingSys::createDescriptors() {
  setLayout = buildDescriptorSetLayout(
      context, {// Binding 0: Storage Image (Output)
                VkDescriptorSetLayoutBinding{
                    .binding = 0,
                    .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                // Binding 1: Color Sampler (Input)
                VkDescriptorSetLayoutBinding{
                    .binding = 1,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                },
                // Binding 2: Depth Sampler (Input)
                VkDescriptorSetLayoutBinding{
                    .binding = 2,
                    .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                    .descriptorCount = 1,
                    .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
                }});
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(setLayout),
                    "post processing set layout");

  auto imageCount = context.vulkan.swapChain->imageCount();
  descriptorSets.reserve(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    auto &set = descriptorSets.emplace_back();
    set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
    VkDescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getImageView(i);
    swapImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageView = context.vulkan.swapChain->getDepthImageView(i);
    depthImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthImageInfo.sampler = context.vulkan.defaultSampler;

    DescriptorWriter writer(context);
    writer.writeImage(0, swapImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.writeImage(1, depthImageInfo,
                      VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
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
    VkDescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getImageView(i);
    swapImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo colorInfo{};
    colorInfo.imageView = context.vulkan.swapChain->getDepthImageView(i);
    colorInfo.imageLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    colorInfo.sampler = context.vulkan.defaultSampler;

    VkDescriptorImageInfo depthInfo{
        .sampler = context.vulkan.defaultSampler,
        .imageView = context.vulkan.swapChain->getDepthImageView(i),
        .imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};

    DescriptorWriter writer(context);
    writer.writeImage(0, swapImageInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.writeImage(1, colorInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.writeImage(2, depthInfo, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE);
    writer.updateSet(descriptorSets[i]);
  }
}

void PostProcessingSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  VkPipelineLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.pSetLayouts = setLayouts.data();
  layoutInfo.setLayoutCount = setLayouts.size();
  pipeline = std::make_unique<ComputePipeline>(
      context, "shaders/postProcessing.comp.spv", layoutInfo,
      "post processing");
}

PostProcessingSys::PostProcessingSys(EngineContext &context) : System(context) {
  createDescriptors();
  createPipeline();
  savedSwapChain = context.vulkan.swapChain.get();
}

void PostProcessingSys::run(VkCommandBuffer cmd, uint32_t imageIndex) {
  if (context.vulkan.swapChain.get() != savedSwapChain) {
    savedSwapChain = context.vulkan.swapChain.get();
    recreateDescriptors();
  }

  VkImageMemoryBarrier barriers[2] = {};

  // Swap chain image: UNDEFINED -> GENERAL
  barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[0].oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; // Handle post-recreation
  barriers[0].newLayout = VK_IMAGE_LAYOUT_GENERAL;
  barriers[0].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[0].image = context.vulkan.swapChain->getImage(imageIndex);
  barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barriers[0].subresourceRange.baseMipLevel = 0;
  barriers[0].subresourceRange.levelCount = 1;
  barriers[0].subresourceRange.baseArrayLayer = 0;
  barriers[0].subresourceRange.layerCount = 1;
  barriers[0].srcAccessMask = 0; // No prior access after recreation
  barriers[0].dstAccessMask =
      VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;

  // Depth image: DEPTH_STENCIL_ATTACHMENT_OPTIMAL ->
  // DEPTH_STENCIL_READ_ONLY_OPTIMAL
  barriers[1].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barriers[1].oldLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
  barriers[1].newLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
  barriers[1].srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barriers[1].image = context.vulkan.swapChain->getDepthImage(imageIndex);
  barriers[1].subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
  barriers[1].subresourceRange.baseMipLevel = 0;
  barriers[1].subresourceRange.levelCount = 1;
  barriers[1].subresourceRange.baseArrayLayer = 0;
  barriers[1].subresourceRange.layerCount = 1;
  barriers[1].srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  barriers[1].dstAccessMask = VK_ACCESS_SHADER_READ_BIT;

  vkCmdPipelineBarrier(cmd,
                       VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT |
                           VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 0, nullptr, 0,
                       nullptr, 2, barriers);
  // Bind pipeline
  pipeline->bind(cmd);

  // Bind descriptor set
  VkDescriptorSet sets[] = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      descriptorSets[imageIndex]};
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE, *pipeline, 0, 2,
                          sets, 0, nullptr);

  // Dispatch compute shader
  uint32_t groupCountX = (context.window.size.x + 15) / 16;
  uint32_t groupCountY = (context.window.size.y + 15) / 16;
  vkCmdDispatch(cmd, groupCountX, groupCountY, 1);

  barriers[0].oldLayout = VK_IMAGE_LAYOUT_GENERAL;
  barriers[0].newLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
  barriers[0].srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barriers[0].dstAccessMask = VK_ACCESS_MEMORY_READ_BIT;

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, 0, 0, nullptr, 0,
                       nullptr, 1, &barriers[0]);
}
} // namespace vkh
