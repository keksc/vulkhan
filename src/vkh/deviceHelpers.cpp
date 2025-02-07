#include "deviceHelpers.hpp"

#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#include <fstream>

#include "swapChain.hpp"

namespace vkh {
SwapChainSupportDetails querySwapChainSupport(EngineContext &context,
                                              VkPhysicalDevice device) {
  SwapChainSupportDetails details;
  vkGetPhysicalDeviceSurfaceCapabilitiesKHR(device, context.vulkan.surface,
                                            &details.capabilities);

  uint32_t formatCount;
  vkGetPhysicalDeviceSurfaceFormatsKHR(device, context.vulkan.surface,
                                       &formatCount, nullptr);

  if (formatCount != 0) {
    details.formats.resize(formatCount);
    vkGetPhysicalDeviceSurfaceFormatsKHR(device, context.vulkan.surface,
                                         &formatCount, details.formats.data());
  }

  uint32_t presentModeCount;
  vkGetPhysicalDeviceSurfacePresentModesKHR(device, context.vulkan.surface,
                                            &presentModeCount, nullptr);

  if (presentModeCount != 0) {
    details.presentModes.resize(presentModeCount);
    vkGetPhysicalDeviceSurfacePresentModesKHR(device, context.vulkan.surface,
                                              &presentModeCount,
                                              details.presentModes.data());
  }
  return details;
}
QueueFamilyIndices findQueueFamilies(EngineContext &context,
                                     VkPhysicalDevice device) {
  QueueFamilyIndices indices;

  uint32_t queueFamilyCount = 0;
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount, nullptr);

  std::vector<VkQueueFamilyProperties> queueFamilies(queueFamilyCount);
  vkGetPhysicalDeviceQueueFamilyProperties(device, &queueFamilyCount,
                                           queueFamilies.data());

  int i = 0;
  for (const auto &queueFamily : queueFamilies) {
    if (queueFamily.queueCount > 0 &&
        queueFamily.queueFlags & VK_QUEUE_GRAPHICS_BIT) {
      indices.graphicsFamily = i;
      indices.graphicsFamilyHasValue = true;
    }
    VkBool32 presentSupport = false;
    vkGetPhysicalDeviceSurfaceSupportKHR(device, i, context.vulkan.surface,
                                         &presentSupport);
    if (queueFamily.queueCount > 0 && presentSupport) {
      indices.presentFamily = i;
      indices.presentFamilyHasValue = true;
    }
    if (queueFamily.queueCount > 0 &&
        queueFamily.queueFlags & VK_QUEUE_COMPUTE_BIT) {
      indices.computeFamily = i;
      indices.computeFamilyHasValue = true;
    }
    if (indices.isComplete()) {
      break;
    }

    i++;
  }

  return indices;
}
uint32_t findMemoryType(EngineContext &context, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties) {
  VkPhysicalDeviceMemoryProperties memProperties;
  vkGetPhysicalDeviceMemoryProperties(context.vulkan.physicalDevice,
                                      &memProperties);
  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}
VkFormat findSupportedFormat(EngineContext &context,
                             const std::vector<VkFormat> &candidates,
                             VkImageTiling tiling,
                             VkFormatFeatureFlags features) {
  for (VkFormat format : candidates) {
    VkFormatProperties props;
    vkGetPhysicalDeviceFormatProperties(context.vulkan.physicalDevice, format,
                                        &props);

    if (tiling == VK_IMAGE_TILING_LINEAR &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == VK_IMAGE_TILING_OPTIMAL &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throw std::runtime_error("failed to find supported format!");
}
void createBuffer(EngineContext &context, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkBuffer &buffer, VkDeviceMemory &bufferMemory) {
  VkBufferCreateInfo bufferInfo{};
  bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
  bufferInfo.size = size;
  bufferInfo.usage = usage;
  bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

  if (vkCreateBuffer(context.vulkan.device, &bufferInfo, nullptr, &buffer) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  VkMemoryRequirements memRequirements;
  vkGetBufferMemoryRequirements(context.vulkan.device, buffer,
                                &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(context, memRequirements.memoryTypeBits, properties);

  if (vkAllocateMemory(context.vulkan.device, &allocInfo, nullptr,
                       &bufferMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate vertex buffer memory!");
  }

  vkBindBufferMemory(context.vulkan.device, buffer, bufferMemory, 0);
}
VkCommandBuffer beginSingleTimeCommands(EngineContext &context) {
  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = context.vulkan.commandPool;
  allocInfo.commandBufferCount = 1;

  VkCommandBuffer commandBuffer;
  vkAllocateCommandBuffers(context.vulkan.device, &allocInfo, &commandBuffer);

  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
  beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;

  vkBeginCommandBuffer(commandBuffer, &beginInfo);
  return commandBuffer;
}

void endSingleTimeCommands(EngineContext &context,
                           VkCommandBuffer commandBuffer) {
  vkEndCommandBuffer(commandBuffer);

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = &commandBuffer;

  vkQueueSubmit(context.vulkan.graphicsQueue, 1, &submitInfo, VK_NULL_HANDLE);
  vkQueueWaitIdle(context.vulkan.graphicsQueue);

  vkFreeCommandBuffers(context.vulkan.device, context.vulkan.commandPool, 1,
                       &commandBuffer);
}
void copyBuffer(EngineContext &context, VkBuffer srcBuffer, VkBuffer dstBuffer,
                VkDeviceSize size) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands(context);

  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = 0; // Optional
  copyRegion.dstOffset = 0; // Optional
  copyRegion.size = size;
  vkCmdCopyBuffer(commandBuffer, srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(context, commandBuffer);
}
struct TransitionParams {
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;
};

TransitionParams getTransitionParams(VkImageLayout oldLayout,
                                     VkImageLayout newLayout) {
  TransitionParams params{};
  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  // NEW: Support transition from TRANSFER_DST_OPTIMAL to GENERAL (e.g. for
  // compute usage)
  else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
           newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    params.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("Unsupported layout transition!");
  }
  return params;
}
bool hasStencilComponent(VkFormat format) {
  return (format == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
         (format == VK_FORMAT_D24_UNORM_S8_UINT);
}
void transitionImageLayout(EngineContext &context, VkImage image,
                           VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands(context);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  // Set aspect mask based on format and intended usage.
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
      newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasStencilComponent(format)) {
      barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  TransitionParams params = getTransitionParams(oldLayout, newLayout);
  barrier.srcAccessMask = params.srcAccessMask;
  barrier.dstAccessMask = params.dstAccessMask;

  vkCmdPipelineBarrier(commandBuffer, params.srcStage, params.dstStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  endSingleTimeCommands(context, commandBuffer);
}
void copyBufferToImage(EngineContext &context, VkBuffer buffer, VkImage image,
                       uint32_t width, uint32_t height) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands(context);

  VkBufferImageCopy region{};
  region.bufferOffset = 0;
  region.bufferRowLength = 0;
  region.bufferImageHeight = 0;

  region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  region.imageSubresource.mipLevel = 0;
  region.imageSubresource.baseArrayLayer = 0;
  region.imageSubresource.layerCount = 1;

  region.imageOffset = {0, 0, 0};
  region.imageExtent = {width, height, 1};

  vkCmdCopyBufferToImage(commandBuffer, buffer, image,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

  endSingleTimeCommands(context, commandBuffer);
}
VkImageView createImageView(EngineContext &context, VkImage image,
                            VkFormat format) {
  VkImageViewCreateInfo viewInfo{};
  viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
  viewInfo.image = image;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = 1;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  VkImageView imageView;
  if (vkCreateImageView(context.vulkan.device, &viewInfo, nullptr,
                        &imageView) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture image view!");
  }
  return imageView;
}
VkImageView createTextureImageView(EngineContext &context, VkImage image) {
  return createImageView(context, image,
                         context.vulkan.swapChain->getSwapChainImageFormat());
}
std::vector<char> readFile(const std::filesystem::path &filepath) {
  std::ifstream file{filepath, std::ios::ate | std::ios::binary};

  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + filepath.string());
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);

  file.close();
  return buffer;
}
VkImage createImageWithInfo(EngineContext &context,
                            const VkImageCreateInfo &imageInfo,
                            VkDeviceMemory &imageMemory) {
  VkImage image;
  if (vkCreateImage(context.vulkan.device, &imageInfo, nullptr, &image) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }

  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(context.vulkan.device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(context, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(context.vulkan.device, &allocInfo, nullptr,
                       &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  if (vkBindImageMemory(context.vulkan.device, image, imageMemory, 0) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to bind image memory!");
  }
  return image;
}
} // namespace vkh
