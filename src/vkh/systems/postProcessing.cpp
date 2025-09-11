#include "postProcessing.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

namespace vkh {
void PostProcessingSys::createDescriptors() {
  setLayout = DescriptorSetLayout::Builder(context)
                  .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                              VK_SHADER_STAGE_COMPUTE_BIT)
                  .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                              VK_SHADER_STAGE_COMPUTE_BIT)
                  .build();

  auto imageCount = context.vulkan.swapChain->imageCount();
  descriptorSets.resize(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    VkDescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getImageView(i);
    swapImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageView = context.vulkan.swapChain->getDepthImageView(i);
    depthImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthImageInfo.sampler = context.vulkan.defaultSampler;

    DescriptorWriter(*setLayout, *context.vulkan.globalDescriptorPool)
        .writeImage(0, &swapImageInfo)
        .writeImage(1, &depthImageInfo)
        .build(descriptorSets[i]);
  }
}

void PostProcessingSys::recreateDescriptors() {
  // Clear old descriptor sets (optional, depending on descriptor pool flags)
  descriptorSets.clear();

  // Recreate descriptor sets for new swap chain images
  auto imageCount = context.vulkan.swapChain->imageCount();
  descriptorSets.resize(imageCount);
  for (size_t i = 0; i < imageCount; i++) {
    VkDescriptorImageInfo swapImageInfo{};
    swapImageInfo.imageView = context.vulkan.swapChain->getImageView(i);
    swapImageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;

    VkDescriptorImageInfo depthImageInfo{};
    depthImageInfo.imageView = context.vulkan.swapChain->getDepthImageView(i);
    depthImageInfo.imageLayout =
        VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL;
    depthImageInfo.sampler = context.vulkan.defaultSampler;

    DescriptorWriter(*setLayout, *context.vulkan.globalDescriptorPool)
        .writeImage(0, &swapImageInfo)
        .writeImage(1, &depthImageInfo)
        .build(descriptorSets[i]);
  }
}

void PostProcessingSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout, *setLayout};

  VkPipelineLayoutCreateInfo layoutInfo{
      .sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
  layoutInfo.pSetLayouts = descriptorSetLayouts.data();
  layoutInfo.setLayoutCount = descriptorSetLayouts.size();
  pipeline = std::make_unique<ComputePipeline>(
      context, "shaders/postProcessing.comp.spv", layoutInfo);
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
  VkDescriptorSet sets[] = {context.frameInfo.globalDescriptorSet,
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
