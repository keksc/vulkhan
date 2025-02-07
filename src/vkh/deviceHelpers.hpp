#pragma once

#include <vulkan/vulkan_core.h>

#include "engineContext.hpp"

#include <filesystem>
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
VkImageView createImageView(EngineContext &context, VkImage image,
                            VkFormat format);
std::vector<char> readFile(const std::filesystem::path &filepath);
uint32_t findMemoryType(EngineContext &context, uint32_t typeFilter,
                        VkMemoryPropertyFlags properties);
VkImage createImage(EngineContext &context, int w, int h,
                    VkDeviceMemory &imageMemory, VkFormat format);
VkImage createImageWithInfo(EngineContext &context,
                            const VkImageCreateInfo &imageInfo,
                            VkDeviceMemory &imageMemory);
VkCommandBuffer beginSingleTimeCommands(EngineContext &context);
void endSingleTimeCommands(EngineContext &context,
                           VkCommandBuffer commandBuffer);
} // namespace vkh
