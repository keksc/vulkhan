#pragma once

#include <vulkan/vulkan_core.h>

#include <filesystem>
#include <optional>

#include "engineContext.hpp"

namespace vkh {
struct ImageCreateInfo {
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  int w = 1;
  int h = 1;
  void *data = nullptr;
  std::optional<uint32_t> color;
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};
class Image {
public:
  Image(EngineContext &context, const ImageCreateInfo &createInfo);
  Image(EngineContext &context, const std::filesystem::path &path,
        bool enableAlpha = true, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  Image(EngineContext &context, const void *data, int len,
        bool enableAlpha = true, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

  operator VkImage() { return img; }
  inline const VkImageView getImageView() { return view; }
  ~Image();

  void copyFromBuffer(
      VkCommandBuffer cmdBuffer, VkBuffer buffer, bool genMips = true,
      VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      uint32_t bufferOffset = 0,
      VkAccessFlags dstAccessMask = VK_ACCESS_SHADER_READ_BIT);

  void transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout);

  static uint32_t formatSize(VkFormat format);

  VkDescriptorImageInfo getDescriptorInfo() {
    return VkDescriptorImageInfo{.sampler = context.vulkan.modelSampler,
                                 .imageView = view,
                                 .imageLayout = layout};
  }

  void copyFromBuffer(
      VkCommandBuffer cmdBuffer, VkBuffer buffer,
      VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      uint32_t bufferOffset = 0,
      VkAccessFlags dstAccessMask = VK_ACCESS_SHADER_READ_BIT);

  int w, h;

  void TransitionLayout_DST_OPTIMALtoSHADER_READ(
      VkCommandBuffer cmdBuffer,
      VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
      VkAccessFlags dstAccessMask = VK_ACCESS_SHADER_READ_BIT);
  void TransitionLayoutToDST_OPTIMAL(
      VkCommandBuffer cmdBuffer,
      VkPipelineStageFlags stage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  void TransitionLayout_SHADER_READtoDST_OPTIMAL(
      VkCommandBuffer cmdBuffer,
      VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT);
  void TransitionLayout_UNDEFtoDST_OPTIMAL(VkCommandBuffer cmdBuffer);
  void TransitionLayout(VkCommandBuffer cmdBuffer, VkImageLayout newLayout,
                        VkPipelineStageFlags srcStageMask,
                        VkPipelineStageFlags dstStageMask);

  void copyBufferToImage_def(VkCommandBuffer cmdBuffer, VkBuffer buffer,
                         uint32_t bufferOffset = 0);

private:
  void RecordImageBarrier(VkCommandBuffer cmdBuffer,
                          VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask,
                          VkAccessFlags srcAccessMask,
                          VkAccessFlags dstAccessMask,
                          VkImageLayout newLayout) const;

  void createImageFromPixels(void *pixels, int w, int h);
  VkImage createImage(EngineContext &context, int w, int h,
                      VkImageUsageFlags usage);

  EngineContext &context;

  VkImage img;
  VkImageView view;
  VkDeviceMemory memory;
  VkFormat format;
  VkImageLayout layout;
};
} // namespace vkh
