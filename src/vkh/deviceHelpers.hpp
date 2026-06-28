#pragma once

#include <vulkan/vulkan.hpp>

#include <filesystem>
#include <vector>

namespace vkh {
class EngineContext;

struct SwapChainSupportDetails {
  vk::SurfaceCapabilitiesKHR capabilities;
  std::vector<vk::SurfaceFormatKHR> formats;
  std::vector<vk::PresentModeKHR> presentModes;
};

struct QueueFamilyIndices {
  uint32_t graphicsFamily;
  uint32_t computeFamily;
  uint32_t presentFamily;
  bool graphicsFamilyHasValue = false;
  bool computeFamilyHasValue = false;
  bool presentFamilyHasValue = false;

  bool isComplete() const {
    return graphicsFamilyHasValue && presentFamilyHasValue &&
           computeFamilyHasValue;
  }
};

SwapChainSupportDetails querySwapChainSupport(EngineContext &context,
                                              vk::PhysicalDevice device);
SwapChainSupportDetails getSwapChainSupport(EngineContext &context);

QueueFamilyIndices findQueueFamilies(EngineContext &context,
                                     vk::PhysicalDevice device);
QueueFamilyIndices findPhysicalQueueFamilies(EngineContext &context);

vk::Format findSupportedFormat(EngineContext &context,
                               const std::vector<vk::Format> &candidates,
                               vk::ImageTiling tiling,
                               vk::FormatFeatureFlags features);

void createBuffer(EngineContext &context, vk::DeviceSize size,
                  vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::Buffer &buffer,
                  vk::DeviceMemory &bufferMemory);

void copyBuffer(EngineContext &context, vk::Buffer srcBuffer,
                vk::Buffer dstBuffer, vk::DeviceSize size);

void copyBufferToImage(EngineContext &context, vk::Buffer buffer,
                       vk::Image image, uint32_t width, uint32_t height,
                       uint32_t offset = 0);

vk::ImageView createImageView(EngineContext &context, vk::Image image,
                              vk::Format format);

std::vector<char> readFile(const std::filesystem::path &filepath);

uint32_t findMemoryType(EngineContext &context, uint32_t typeFilter,
                        vk::MemoryPropertyFlags properties);

vk::Image createImage(EngineContext &context, int w, int h,
                      vk::DeviceMemory &imageMemory, vk::Format format);

vk::Image createImageWithInfo(EngineContext &context,
                              const vk::ImageCreateInfo &imageInfo,
                              vk::DeviceMemory &imageMemory);

vk::CommandBuffer beginSingleTimeCommands(EngineContext &context);

void endSingleTimeCommands(EngineContext &context,
                           vk::CommandBuffer commandBuffer, vk::Queue queue);

size_t getNonCoherentAtomSizeAlignment(EngineContext &context,
                                       size_t originalSize);

size_t getUniformBufferAlignment(EngineContext &context, size_t originalSize);
} // namespace vkh
