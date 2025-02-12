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
VkImage Image::createImage(EngineContext &context, int w, int h,
                           VkImageUsageFlags usage) {
  VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = usage,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  imageInfo.extent.width = w;
  imageInfo.extent.height = h;
  imageInfo.extent.depth = 1;

  VkImage image;
  if (vkCreateImage(context.vulkan.device, &imageInfo, nullptr, &image) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(context.vulkan.device, image, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(context, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(context.vulkan.device, &allocInfo, nullptr,
                       &imageMemory) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  if (vkBindImageMemory(context.vulkan.device, image, imageMemory, 0) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to bind image memory!");
  }
  return image;
}
void Image::createImageFromPixels(void *pixels, int w, int h) {
  VkDeviceSize imageSize = w * h * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory !");
  }

  image =
      createImage(context, w, h,
                  VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = imageSize;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(pixels);

  stbi_image_free(pixels);

  transitionLayout(VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer, image, static_cast<uint32_t>(w),
                    static_cast<uint32_t>(w));
  transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                   VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  imageView = createImageView(context, image, format);
}
Image::Image(EngineContext &context, const void *data, int len,
             bool enableAlpha, VkFormat format)
    : context{context}, format{format} {
  int w, h, texChannels;
  stbi_uc *pixels =
      stbi_load_from_memory((const stbi_uc *)data, len, &w, &h, &texChannels,
                            enableAlpha ? STBI_rgb_alpha : STBI_rgb);
  createImageFromPixels(pixels, w, h);
}
Image::Image(EngineContext &context, const std::filesystem::path &path,
             bool enableAlpha, VkFormat format)
    : context{context}, format{format} {
  int w, h, texChannels;
  stbi_uc *pixels = stbi_load(path.string().c_str(), &w, &h, &texChannels,
                              enableAlpha ? STBI_rgb_alpha : STBI_rgb);
  createImageFromPixels(pixels, w, h);
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
    : context{context}, format{createInfo.format} {
  image = createImage(context, createInfo.w, createInfo.h, createInfo.usage);
  imageView = createImageView(context, image, format);
  if (!createInfo.data && !createInfo.color.has_value()) {
    return;
  }

  void *data;
  VkDeviceSize size;
  if (createInfo.data) {
    data = createInfo.data;
    size = getImageSize(createInfo.w, createInfo.h, createInfo.format);
  } else {
    uint32_t color = createInfo.color.value();
    data = static_cast<void *>(&color);
    size = sizeof(uint32_t);
  }
  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = size;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(data);

  transitionLayout(VK_IMAGE_LAYOUT_UNDEFINED,
                   VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer, image, createInfo.w, createInfo.h);
  transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, createInfo.layout);
}
Image::~Image() {
  vkDestroyImage(context.vulkan.device, image, nullptr);
  vkDestroyImageView(context.vulkan.device, imageView, nullptr);
  vkFreeMemory(context.vulkan.device, imageMemory, nullptr);
}
struct TransitionParams {
  VkAccessFlags srcAccessMask;
  VkAccessFlags dstAccessMask;
  VkPipelineStageFlags srcStage;
  VkPipelineStageFlags dstStage;
};

TransitionParams getTransitionParams(VkImageLayout oldLayout,
                                     VkImageLayout newLayout) {
  TransitionParams params{};
  if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
      newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  }
  // NEW: Support transition from TRANSFER_DST_OPTIMAL to GENERAL (e.g. for
  // compute usage)
  else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
           newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    params.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
                           VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask =
        VK_ACCESS_SHADER_READ_BIT | VK_ACCESS_SHADER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_GENERAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
  } else {
    throw std::invalid_argument("Unsupported layout transition!");
  }
  return params;
}
bool hasStencilComponent(VkFormat format) {
  return (format == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
         (format == VK_FORMAT_D24_UNORM_S8_UINT);
}
void Image::transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
  VkCommandBuffer commandBuffer = beginSingleTimeCommands(context);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = image;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;

  // Set aspect mask based on format and intended usage.
  if (newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL ||
      newLayout == VK_IMAGE_LAYOUT_DEPTH_STENCIL_READ_ONLY_OPTIMAL) {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    if (hasStencilComponent(format)) {
      barrier.subresourceRange.aspectMask |= VK_IMAGE_ASPECT_STENCIL_BIT;
    }
  } else {
    barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  }

  TransitionParams params = getTransitionParams(oldLayout, newLayout);
  barrier.srcAccessMask = params.srcAccessMask;
  barrier.dstAccessMask = params.dstAccessMask;

  vkCmdPipelineBarrier(commandBuffer, params.srcStage, params.dstStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);

  endSingleTimeCommands(context, commandBuffer);
}
} // namespace vkh
