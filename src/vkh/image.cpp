#include "image.hpp"
#include <vulkan/vulkan_core.h>

#define STB_IMAGE_IMPLEMENTATION
#include <fmt/format.h>
#include <stb/stb_image.h>

#include "buffer.hpp"
#include "debug.hpp"
#include "deviceHelpers.hpp"

namespace vkh {
Image::Image(EngineContext &context, const std::string &name,
             const std::string &path, VkFormat format)
    : context{context} {
  int w, h, texChannels;
  stbi_uc *pixels =
      stbi_load(path.c_str(), &w, &h, &texChannels, STBI_rgb_alpha);
  VkDeviceSize imageSize = w * h * 4;

  if (!pixels) {
    throw std::runtime_error(
        fmt::format("failed to load texture image: {}!", name));
  }

  image = createImage(context, w, h, imageMemory, format);

  Buffer stagingBuffer(context, name, imageSize, 1,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  stagingBuffer.map();
  stagingBuffer.writeToBuffer(pixels);

  stbi_image_free(pixels);

  transitionImageLayout(context, image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer.getBuffer(), image,
                    static_cast<uint32_t>(w), static_cast<uint32_t>(w));
  transitionImageLayout(context, image, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  imageView = createImageView(context, image, format);

  debug::setObjName(context, name, VK_OBJECT_TYPE_IMAGE, image);
}
Image::Image(EngineContext &context, const std::string &name, uint32_t color,
             int w, int h, VkFormat format)
    : context{context} {
  image = createImage(context, w, h, imageMemory, format);

  Buffer stagingBuffer(context, name, sizeof(uint32_t), 1,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  stagingBuffer.map();
  stagingBuffer.writeToBuffer(&color);

  transitionImageLayout(context, image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer.getBuffer(), image, 1, 1);
  transitionImageLayout(context, image, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  imageView = createImageView(context, image, format);
  debug::setObjName(context, name, VK_OBJECT_TYPE_IMAGE, image);
}
Image::~Image() {
  vkDestroyImage(context.vulkan.device, image, nullptr);
  vkDestroyImageView(context.vulkan.device, imageView, nullptr);
  vkFreeMemory(context.vulkan.device, imageMemory, nullptr);
}
Image::Image(EngineContext &context, const std::string &name, int w, int h,
             void *data, VkFormat format)
    : context{context} {
  VkDeviceSize imageSize = w * h * 4;
  image = createImage(context, w, h, imageMemory, format);

  Buffer stagingBuffer(context, name, imageSize, 1,
                       VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                       VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                           VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
  stagingBuffer.map();
  stagingBuffer.writeToBuffer(data);

  transitionImageLayout(context, image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer.getBuffer(), image, w, h);
  transitionImageLayout(context, image, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  imageView = createImageView(context, image, format);
  debug::setObjName(context, name, VK_OBJECT_TYPE_IMAGE, image);
}
} // namespace vkh
