#include "image.hpp"
#include <vulkan/vulkan_core.h>

#include <fmt/format.h>
#define STB_IMAGE_IMPLEMENTATION
#ifdef WIN32
#include <stb_image.h>
#else
#include <stb/stb_image.h>
#endif

#include <unordered_map>

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
Image::Image(EngineContext &context, const std::filesystem::path &path,
             bool enableAlpha, VkFormat format)
    : context{context} {
  int w, h, texChannels;
  stbi_uc *pixels = stbi_load(path.c_str(), &w, &h, &texChannels,
                              enableAlpha ? STBI_rgb_alpha : STBI_rgb);
  createImageFromPixels(pixels, w, h, format);
}
static const std::unordered_map<VkFormat, uint32_t> formatBytesPerPixel = {
    // 8-bit formats (1 byte per pixel)
    {VK_FORMAT_R8_UNORM, 1},
    {VK_FORMAT_R8_SNORM, 1},
    {VK_FORMAT_R8_USCALED, 1},
    {VK_FORMAT_R8_SSCALED, 1},
    {VK_FORMAT_R8_UINT, 1},
    {VK_FORMAT_R8_SINT, 1},
    {VK_FORMAT_R8_SRGB, 1},

    // 2-channel 8-bit formats (2 bytes per pixel)
    {VK_FORMAT_R8G8_UNORM, 2},
    {VK_FORMAT_R8G8_SNORM, 2},
    {VK_FORMAT_R8G8_USCALED, 2},
    {VK_FORMAT_R8G8_SSCALED, 2},
    {VK_FORMAT_R8G8_UINT, 2},
    {VK_FORMAT_R8G8_SINT, 2},
    {VK_FORMAT_R8G8_SRGB, 2},

    // 3-channel 8-bit formats (3 bytes per pixel)
    {VK_FORMAT_R8G8B8_UNORM, 3},
    {VK_FORMAT_R8G8B8_SNORM, 3},
    {VK_FORMAT_R8G8B8_USCALED, 3},
    {VK_FORMAT_R8G8B8_SSCALED, 3},
    {VK_FORMAT_R8G8B8_UINT, 3},
    {VK_FORMAT_R8G8B8_SINT, 3},
    {VK_FORMAT_R8G8B8_SRGB, 3},
    {VK_FORMAT_B8G8R8_UNORM, 3},
    {VK_FORMAT_B8G8R8_SNORM, 3},
    {VK_FORMAT_B8G8R8_USCALED, 3},
    {VK_FORMAT_B8G8R8_SSCALED, 3},
    {VK_FORMAT_B8G8R8_UINT, 3},
    {VK_FORMAT_B8G8R8_SINT, 3},
    {VK_FORMAT_B8G8R8_SRGB, 3},

    // 4-channel 8-bit formats (4 bytes per pixel)
    {VK_FORMAT_R8G8B8A8_UNORM, 4},
    {VK_FORMAT_R8G8B8A8_SNORM, 4},
    {VK_FORMAT_R8G8B8A8_USCALED, 4},
    {VK_FORMAT_R8G8B8A8_SSCALED, 4},
    {VK_FORMAT_R8G8B8A8_UINT, 4},
    {VK_FORMAT_R8G8B8A8_SINT, 4},
    {VK_FORMAT_R8G8B8A8_SRGB, 4},
    {VK_FORMAT_B8G8R8A8_UNORM, 4},
    {VK_FORMAT_B8G8R8A8_SNORM, 4},
    {VK_FORMAT_B8G8R8A8_USCALED, 4},
    {VK_FORMAT_B8G8R8A8_SSCALED, 4},
    {VK_FORMAT_B8G8R8A8_UINT, 4},
    {VK_FORMAT_B8G8R8A8_SINT, 4},
    {VK_FORMAT_B8G8R8A8_SRGB, 4},
    {VK_FORMAT_A8B8G8R8_UNORM_PACK32, 4},
    {VK_FORMAT_A8B8G8R8_SNORM_PACK32, 4},
    {VK_FORMAT_A8B8G8R8_USCALED_PACK32, 4},
    {VK_FORMAT_A8B8G8R8_SSCALED_PACK32, 4},
    {VK_FORMAT_A8B8G8R8_UINT_PACK32, 4},
    {VK_FORMAT_A8B8G8R8_SINT_PACK32, 4},
    {VK_FORMAT_A8B8G8R8_SRGB_PACK32, 4},

    // 16-bit single channel
    {VK_FORMAT_R16_UNORM, 2},
    {VK_FORMAT_R16_SNORM, 2},
    {VK_FORMAT_R16_USCALED, 2},
    {VK_FORMAT_R16_SSCALED, 2},
    {VK_FORMAT_R16_UINT, 2},
    {VK_FORMAT_R16_SINT, 2},
    {VK_FORMAT_R16_SFLOAT, 2},

    // 2-channel 16-bit formats
    {VK_FORMAT_R16G16_UNORM, 4},
    {VK_FORMAT_R16G16_SNORM, 4},
    {VK_FORMAT_R16G16_USCALED, 4},
    {VK_FORMAT_R16G16_SSCALED, 4},
    {VK_FORMAT_R16G16_UINT, 4},
    {VK_FORMAT_R16G16_SINT, 4},
    {VK_FORMAT_R16G16_SFLOAT, 4},

    // 3-channel 16-bit formats
    {VK_FORMAT_R16G16B16_UNORM, 6},
    {VK_FORMAT_R16G16B16_SNORM, 6},
    {VK_FORMAT_R16G16B16_USCALED, 6},
    {VK_FORMAT_R16G16B16_SSCALED, 6},
    {VK_FORMAT_R16G16B16_UINT, 6},
    {VK_FORMAT_R16G16B16_SINT, 6},
    {VK_FORMAT_R16G16B16_SFLOAT, 6},

    // 4-channel 16-bit formats
    {VK_FORMAT_R16G16B16A16_UNORM, 8},
    {VK_FORMAT_R16G16B16A16_SNORM, 8},
    {VK_FORMAT_R16G16B16A16_USCALED, 8},
    {VK_FORMAT_R16G16B16A16_SSCALED, 8},
    {VK_FORMAT_R16G16B16A16_UINT, 8},
    {VK_FORMAT_R16G16B16A16_SINT, 8},
    {VK_FORMAT_R16G16B16A16_SFLOAT, 8},

    // 32-bit formats
    {VK_FORMAT_R32_UINT, 4},
    {VK_FORMAT_R32_SINT, 4},
    {VK_FORMAT_R32_SFLOAT, 4},
    {VK_FORMAT_R32G32_UINT, 8},
    {VK_FORMAT_R32G32_SINT, 8},
    {VK_FORMAT_R32G32_SFLOAT, 8},
    {VK_FORMAT_R32G32B32_UINT, 12},
    {VK_FORMAT_R32G32B32_SINT, 12},
    {VK_FORMAT_R32G32B32_SFLOAT, 12},
    {VK_FORMAT_R32G32B32A32_UINT, 16},
    {VK_FORMAT_R32G32B32A32_SINT, 16},
    {VK_FORMAT_R32G32B32A32_SFLOAT, 16},

    // Packed 32-bit formats
    {VK_FORMAT_B10G11R11_UFLOAT_PACK32, 4},
    {VK_FORMAT_E5B9G9R9_UFLOAT_PACK32, 4},

    // Depth / stencil formats (if needed for copies)
    {VK_FORMAT_D16_UNORM, 2},
    {VK_FORMAT_X8_D24_UNORM_PACK32, 4},
    {VK_FORMAT_D32_SFLOAT, 4},
    {VK_FORMAT_D16_UNORM_S8_UINT,
     3}, // Typically 2 bytes depth + 1 byte stencil
    {VK_FORMAT_D24_UNORM_S8_UINT, 4}, // Typically 3+1 bytes
    {VK_FORMAT_D32_SFLOAT_S8_UINT, 5} // Typically 4+1 bytes
};
VkDeviceSize getImageSize(int w, int h, VkFormat format) {
  auto it = formatBytesPerPixel.find(format);
  if (it == formatBytesPerPixel.end())
    throw std::runtime_error("Unsupported image format");
  uint32_t bytesPerPixel = it->second;
  return w * h * bytesPerPixel;
}
Image::Image(EngineContext &context, const ImageCreateInfo &createInfo)
    : context{context} {
  VkFormat format = createInfo.format;
  VkDeviceSize imageSize = getImageSize(createInfo.w, createInfo.h, format);
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
                        createInfo.layout);

  imageView = createImageView(context, image, format);
}
Image::~Image() {
  vkDestroyImage(context.vulkan.device, image, nullptr);
  vkDestroyImageView(context.vulkan.device, imageView, nullptr);
  vkFreeMemory(context.vulkan.device, imageMemory, nullptr);
}
} // namespace vkh
