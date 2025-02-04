#include "image.hpp"
#include <vulkan/vulkan_core.h>

#include <fmt/format.h>
#define STB_IMAGE_IMPLEMENTATION
#ifdef WIN32
#include <stb_image.h>
#else
#include <stb/stb_image.h>
#endif

#include "buffer.hpp"
#include "deviceHelpers.hpp"

namespace vkh {
void Image::createImageFromPixels(void *pixels, int w, int h, VkFormat format) {
  VkDeviceSize imageSize = w * h * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory !");
  }

  image = createImage(context, w, h, imageMemory, format);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = imageSize;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(pixels);

  stbi_image_free(pixels);

  transitionImageLayout(context, image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer, image, static_cast<uint32_t>(w),
                    static_cast<uint32_t>(w));
  transitionImageLayout(context, image, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  imageView = createImageView(context, image, format);
}
Image::Image(EngineContext &context, const void *data, int len,
             bool enableAlpha, VkFormat format)
    : context{context} {
  int w, h, texChannels;
  stbi_uc *pixels =
      stbi_load_from_memory((const stbi_uc *)data, len, &w, &h, &texChannels,
                            enableAlpha ? STBI_rgb_alpha : STBI_rgb);
  createImageFromPixels(pixels, w, h, format);
}
Image::Image(EngineContext &context, const std::string &path, bool enableAlpha,
             VkFormat format)
    : context{context} {
  int w, h, texChannels;
  stbi_uc *pixels = stbi_load(path.c_str(), &w, &h, &texChannels,
                              enableAlpha ? STBI_rgb_alpha : STBI_rgb);
  createImageFromPixels(pixels, w, h, format);
}
Image::Image(EngineContext &context, const ImageCreateInfo &createInfo)
    : context{context} {
  VkFormat format = createInfo.format;
  VkDeviceSize imageSize = createInfo.w * createInfo.h * 4;
  image = createImage(context, createInfo.w, createInfo.h, imageMemory,
                      createInfo.format);

  uint32_t color = 0xffffffff;
  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = (createInfo.data) ? imageSize : sizeof(uint32_t);
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write((createInfo.data) ? createInfo.data : &color);

  transitionImageLayout(context, image, format, VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer, image, createInfo.w, createInfo.h);
  transitionImageLayout(context, image, format,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  imageView = createImageView(context, image, format);
}
Image::~Image() {
  vkDestroyImage(context.vulkan.device, image, nullptr);
  vkDestroyImageView(context.vulkan.device, imageView, nullptr);
  vkFreeMemory(context.vulkan.device, imageMemory, nullptr);
}
} // namespace vkh
