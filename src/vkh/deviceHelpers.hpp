#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "engineContext.hpp"

#include <vector>

namespace vkh {
struct SwapChainSupportDetails {
  VkSurfaceCapabilitiesKHR capabilities;
  std::vector<VkSurfaceFormatKHR> formats;
  std::vector<VkPresentModeKHR> presentModes;
};
struct QueueFamilyIndices {
  uint32_t graphicsFamily;
  uint32_t computeFamily;
  uint32_t presentFamily;
  bool graphicsFamilyHasValue = false;
  bool computeFamilyHasValue = false;
  bool presentFamilyHasValue = false;
  bool isComplete() {
    return graphicsFamilyHasValue && presentFamilyHasValue &&
           computeFamilyHasValue;
  }
};
SwapChainSupportDetails querySwapChainSupport(EngineContext &context,
                                              VkPhysicalDevice device);
inline SwapChainSupportDetails getSwapChainSupport(EngineContext &context) {
  return querySwapChainSupport(context, context.vulkan.physicalDevice);
}
QueueFamilyIndices findQueueFamilies(EngineContext &context,
                                     VkPhysicalDevice device);
inline QueueFamilyIndices findPhysicalQueueFamilies(EngineContext &context) {
  return findQueueFamilies(context, context.vulkan.physicalDevice);
}
void createImageWithInfo(EngineContext &context,
                         const VkImageCreateInfo &imageInfo,
                         VkMemoryPropertyFlags properties, VkImage &image,
                         VkDeviceMemory &imageMemory);
VkFormat findSupportedFormat(EngineContext &context,
                             const std::vector<VkFormat> &candidates,
                             VkImageTiling tiling,
                             VkFormatFeatureFlags features);
void createBuffer(EngineContext &context, VkDeviceSize size,
                  VkBufferUsageFlags usage, VkMemoryPropertyFlags properties,
                  VkBuffer &buffer, VkDeviceMemory &bufferMemory);
void copyBuffer(EngineContext &context, VkBuffer srcBuffer, VkBuffer dstBuffer,
                VkDeviceSize size);
void transitionImageLayout(EngineContext &context, VkImage image,
                           VkFormat format, VkImageLayout oldLayout,
                           VkImageLayout newLayout);
void copyBufferToImage(EngineContext &context, VkBuffer buffer, VkImage image,
                       uint32_t width, uint32_t height);
VkImage createTextureImage(EngineContext &context, VkDeviceMemory &imageMemory,
                           const char *texturePath);
VkImageView createImageView(EngineContext &context, VkImage image,
                            VkFormat format);
VkSampler createTextureSampler(EngineContext &context);
} // namespace vkh
