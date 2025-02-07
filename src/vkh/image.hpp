#pragma once

#include <vulkan/vulkan_core.h>

#include <filesystem>

#include "engineContext.hpp"

namespace vkh {
struct ImageCreateInfo {
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  int w = 1;
  int h = 1;
  void *data = nullptr;
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};
class Image {
public:
  Image(EngineContext &context, const ImageCreateInfo &createInfo);
  Image(EngineContext &context, const std::filesystem::path &path,
        bool enableAlpha = true, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  Image(EngineContext &context, const void *data, int len,
        bool enableAlpha = true, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

  operator VkImage() { return image; }
  inline const VkImageView getImageView() { return imageView; }
  ~Image();

private:
  void createImageFromPixels(void *pixels, int w, int h, VkFormat format);
  VkImage createImage(EngineContext &context, int w, int h,
                      VkImageUsageFlags usage, VkFormat format);

  EngineContext &context;

  VkImage image;
  VkImageView imageView;
  VkDeviceMemory imageMemory;
};
} // namespace vkh
