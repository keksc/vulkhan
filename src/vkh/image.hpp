#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "engineContext.hpp"

namespace vkh {
struct ImageCreateInfo {
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  int w = 1;
  int h = 1;
  void *data = nullptr;
};
class Image {
public:
  Image(EngineContext &context, const ImageCreateInfo &createInfo);
  Image(EngineContext &context, const std::string &path,
        bool enableAlpha = true, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  Image(EngineContext &context, const void *data, int len,
        bool enableAlpha = true, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

  operator VkImage() { return image; }
  inline const VkImageView getImageView() { return imageView; }
  ~Image();

private:
  void createImageFromPixels(void *pixels, int w, int h, VkFormat format);
  EngineContext &context;
  VkImage image;
  VkImageView imageView;
  VkDeviceMemory imageMemory;
};
} // namespace vkh
