#pragma once

#include <vulkan/vulkan_core.h>

#include <filesystem>

#include "engineContext.hpp"

namespace vkh {
struct ImageCreateInfo {
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  unsigned int w = 1;
  unsigned int h = 1;
  void *data = nullptr;
  uint32_t color;
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};
class Image {
public:
  Image(EngineContext &context, uint32_t w, uint32_t h, VkFormat format,
        uint32_t usage, VkImageLayout layout);
  Image(EngineContext &context, const ImageCreateInfo &createInfo);
  Image(EngineContext &context, const std::filesystem::path &path);
  Image(EngineContext &context, void *data, size_t dataSize);

  operator VkImage() { return img; }
  inline const VkImageView getImageView() { return view; }
  ~Image();

  void copyFromBuffer(VkBuffer buffer, uint32_t bufferOffset = 0);
  void recordCopyFromBuffer(VkCommandBuffer cmd, VkBuffer buffer,
                            uint32_t bufferOffset = 0);

  void transitionLayout(VkImageLayout newLayout);

  static uint32_t getFormatSize(VkFormat format);

  VkDescriptorImageInfo getDescriptorInfo(VkSampler sampler) {
    return VkDescriptorImageInfo{
        .sampler = sampler, .imageView = view, .imageLayout = layout};
  }

  unsigned int w, h;
  unsigned int mipLevels = 1;

  void recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                              VkImageSubresourceRange subresourceRange = {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1});

private:
  void RecordImageBarrier(VkCommandBuffer cmd,
                          VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask,
                          VkAccessFlags srcAccessMask,
                          VkAccessFlags dstAccessMask,
                          VkImageLayout newLayout) const;

  void createImageFromData(void *pixels, size_t dataSize);
  void createImage(EngineContext &context, int w, int h,
                   VkImageUsageFlags usage);
  struct TransitionParams {
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;
  };
  TransitionParams getTransitionParams(VkImageLayout oldLayout,
                                       VkImageLayout newLayout);

  EngineContext &context;

  VkImage img;
  VkImageView view;
  VkDeviceMemory memory;
  VkFormat format;
  VkImageLayout layout;

  VkImageMemoryBarrier undef2DstBarrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = 0,
      .dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,

      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = img,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  VkImageMemoryBarrier dst2ShaderReadOptiBarrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT,
      .dstAccessMask = VK_ACCESS_SHADER_READ_BIT,
      .oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,

      .newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = img,
      .subresourceRange = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};
};
} // namespace vkh
