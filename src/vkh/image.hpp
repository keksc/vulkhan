#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "engineContext.hpp"

namespace vkh {
class Image {
public:
  Image(EngineContext &context, const std::string &name,
        const std::string &path, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  Image(EngineContext &context, const std::string &name, uint32_t color, int w,
        int h, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);
  Image(EngineContext &context, const std::string &name, int w, int h,
        void *data, VkFormat format = VK_FORMAT_R8G8B8A8_SRGB);

  operator VkImage() { return image; }
  inline const VkImage getImage() { return image; }
  inline const VkImageView getImageView() { return imageView; }
  ~Image();

private:
  EngineContext &context;
  VkImage image;
  VkImageView imageView;
  VkDeviceMemory imageMemory;
};
} // namespace vkh
