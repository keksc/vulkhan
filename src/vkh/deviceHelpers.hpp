#pragma once

#include <filesystem>
#include <vector>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include "engineContext.hpp"

namespace vkh {

struct QueueFamilyIndices {
  uint32_t graphicsFamily;
  uint32_t presentFamily;
  uint32_t computeFamily;
  bool graphicsFamilyHasValue = false;
  bool presentFamilyHasValue = false;
  bool computeFamilyHasValue = false;
  bool isComplete() const {
    return graphicsFamilyHasValue && presentFamilyHasValue &&
           computeFamilyHasValue;
  }
};

struct SwapChainSupportDetails {
  vk::SurfaceCapabilitiesKHR capabilities;
  std::vector<vk::SurfaceFormatKHR> formats;
  std::vector<vk::PresentModeKHR> presentModes;
};

SwapChainSupportDetails getSwapChainSupport(EngineContext &context);
QueueFamilyIndices findPhysicalQueueFamilies(EngineContext &context);
SwapChainSupportDetails querySwapChainSupport(EngineContext &context,
                                              vk::PhysicalDevice device);
QueueFamilyIndices findQueueFamilies(EngineContext &context,
                                     vk::PhysicalDevice device);

// Retained for any callers that still need a raw memory-type index (e.g.
// manual allocations outside VMA). Buffer/Image creation below no longer
// use this internally.
uint32_t findMemoryType(EngineContext &context, uint32_t typeFilter,
                        vk::MemoryPropertyFlags properties);

vk::Format findSupportedFormat(EngineContext &context,
                               const std::vector<vk::Format> &candidates,
                               vk::ImageTiling tiling,
                               vk::FormatFeatureFlags features);

// Creates a buffer and backs it with a VMA allocation. `properties` is used
// only to decide whether the allocation should be host-accessible; VMA picks
// the concrete memory type itself.
void createBuffer(EngineContext &context, vk::DeviceSize size,
                  vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::Buffer &buffer,
                  VmaAllocation &allocation,
                  VmaAllocationCreateFlags vmaFlags = 0);

vk::CommandBuffer beginSingleTimeCommands(EngineContext &context);
void endSingleTimeCommands(EngineContext &context,
                           vk::CommandBuffer commandBuffer, vk::Queue queue);

void copyBuffer(EngineContext &context, vk::Buffer srcBuffer,
                vk::Buffer dstBuffer, vk::DeviceSize size);
void copyBufferToImage(EngineContext &context, vk::Buffer buffer,
                       vk::Image image, uint32_t width, uint32_t height,
                       uint32_t offset = 0);

vk::ImageView createImageView(EngineContext &context, vk::Image image,
                              vk::Format format);
vk::ImageView createTextureImageView(EngineContext &context, vk::Image image);

std::vector<char> readFile(const std::filesystem::path &filepath);
void writeFile(const std::filesystem::path &filepath, const void *data,
              size_t size);

// Creates an image and backs it with a VMA allocation.
vk::Image createImageWithInfo(EngineContext &context,
                              const vk::ImageCreateInfo &imageInfo,
                              VmaAllocation &allocation,
                              VmaMemoryUsage memoryUsage = VMA_MEMORY_USAGE_AUTO);

size_t getNonCoherentAtomSizeAlignment(EngineContext &context,
                                       size_t originalSize);
size_t getUniformBufferAlignment(EngineContext &context, size_t originalSize);

} // namespace vkh
