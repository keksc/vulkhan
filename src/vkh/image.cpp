#include "image.hpp"
#include <stdexcept>
#include <vulkan/vulkan_core.h>

#include <fmt/format.h>
#define STB_IMAGE_IMPLEMENTATION
#ifdef _WIN32
#include <stb_image.h>
#else
#include <stb/stb_image.h>
#endif

#include "buffer.hpp"
#include "deviceHelpers.hpp"

namespace vkh {
void Image::recordTransitionLayout(VkCommandBuffer cmdBuffer,
                                   VkImageLayout oldLayout,
                                   VkImageLayout newLayout) {
  if (oldLayout == newLayout)
    return;

  TransitionParams params = getTransitionParams(oldLayout, newLayout);

  VkImageMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
  barrier.oldLayout = oldLayout;
  barrier.newLayout = newLayout;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.image = img;
  barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
  barrier.subresourceRange.baseMipLevel = 0;
  barrier.subresourceRange.levelCount = 1;
  barrier.subresourceRange.baseArrayLayer = 0;
  barrier.subresourceRange.layerCount = 1;
  barrier.srcAccessMask = params.srcAccessMask;
  barrier.dstAccessMask = params.dstAccessMask;

  vkCmdPipelineBarrier(cmdBuffer, params.srcStage, params.dstStage, 0, 0,
                       nullptr, 0, nullptr, 1, &barrier);
  layout = newLayout;
}

void Image::createImage(EngineContext &context, int w, int h,
                        VkImageUsageFlags usage) {
  const VkImageLayout initLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = usage,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = initLayout};
  layout = initLayout;

  imageInfo.extent.width = w;
  imageInfo.extent.height = h;
  imageInfo.extent.depth = 1;

  if (vkCreateImage(context.vulkan.device, &imageInfo, nullptr, &img) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create image!");
  }
  VkMemoryRequirements memRequirements;
  vkGetImageMemoryRequirements(context.vulkan.device, img, &memRequirements);

  VkMemoryAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(context, memRequirements.memoryTypeBits,
                     VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

  if (vkAllocateMemory(context.vulkan.device, &allocInfo, nullptr, &memory) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  if (vkBindImageMemory(context.vulkan.device, img, memory, 0) != VK_SUCCESS) {
    throw std::runtime_error("failed to bind image memory!");
  }
}

void Image::createImageFromPixels(void *pixels, int w, int h) {
  this->w = w;
  this->h = h;
  VkDeviceSize imageSize = w * h * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory!");
  }

  createImage(context, w, h,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  imageSize);
  stagingBuffer.map();
  stagingBuffer.write(pixels);
  stbi_image_free(pixels);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage_def(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  view = createImageView(context, img, format);
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

uint32_t Image::formatSize(VkFormat format) {
  switch (format) {
  case VK_FORMAT_R8_SINT:
  case VK_FORMAT_R8_SNORM:
  case VK_FORMAT_R8_UINT:
  case VK_FORMAT_R8_UNORM:
    return 1;
  case VK_FORMAT_R8G8_SINT:
  case VK_FORMAT_R8G8_SNORM:
  case VK_FORMAT_R8G8_UINT:
  case VK_FORMAT_R8G8_UNORM:
  case VK_FORMAT_R16_SFLOAT:
  case VK_FORMAT_R16_SINT:
  case VK_FORMAT_R16_UINT:
    return 2;
  case VK_FORMAT_B8G8R8A8_SRGB:
  case VK_FORMAT_R8G8B8A8_SRGB:
  case VK_FORMAT_B8G8R8A8_UNORM:
  case VK_FORMAT_R8G8B8A8_SINT:
  case VK_FORMAT_R8G8B8A8_SNORM:
  case VK_FORMAT_R8G8B8A8_UINT:
  case VK_FORMAT_R8G8B8A8_UNORM:
  case VK_FORMAT_R16G16_SFLOAT:
  case VK_FORMAT_R16G16_SINT:
  case VK_FORMAT_R16G16_UINT:
  case VK_FORMAT_R32_SFLOAT:
  case VK_FORMAT_R32_SINT:
  case VK_FORMAT_R32_UINT:
    return 4;
  case VK_FORMAT_R16G16B16A16_SFLOAT:
  case VK_FORMAT_R16G16B16A16_SINT:
  case VK_FORMAT_R16G16B16A16_UINT:
  case VK_FORMAT_R32G32_SFLOAT:
  case VK_FORMAT_R32G32_SINT:
  case VK_FORMAT_R32G32_UINT:
    return 4 * 2;
  case VK_FORMAT_R32G32B32A32_SFLOAT:
  case VK_FORMAT_R32G32B32A32_SINT:
  case VK_FORMAT_R32G32B32A32_UINT:
    return 4 * 4;
  default:
    throw std::runtime_error("Unsupported format for formatSize");
  }
}
Image::Image(EngineContext &context, uint32_t w, uint32_t h, VkFormat format,
             uint32_t usage, VkImageLayout layout)
    : context{context}, w{w}, h{h}, format{format}, layout{layout} {
  createImage(context, w, h, usage);
  view = createImageView(context, img, format);
}
Image::Image(EngineContext &context, const ImageCreateInfo &createInfo)
    : context{context}, format{createInfo.format}, w{createInfo.w},
      h{createInfo.h}, layout{createInfo.layout} {
  createImage(context, createInfo.w, createInfo.h, createInfo.usage);
  view = createImageView(context, img, format);

  void *data;
  VkDeviceSize size;
  if (createInfo.data) {
    data = createInfo.data;
    size = createInfo.w * createInfo.h * Image::formatSize(format);
  } else {
    uint32_t color = createInfo.color;
    data = static_cast<void *>(&color);
    size = sizeof(uint32_t);
  }
  Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  size);
  stagingBuffer.map();
  stagingBuffer.write(data);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_UNDEFINED,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage_def(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         createInfo.layout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

Image::~Image() {
  vkDestroyImage(context.vulkan.device, img, nullptr);
  vkDestroyImageView(context.vulkan.device, view, nullptr);
  vkFreeMemory(context.vulkan.device, memory, nullptr);
}

Image::TransitionParams Image::getTransitionParams(VkImageLayout oldLayout,
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
  } else if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL &&
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
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_GENERAL) {
    params.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.dstAccessMask =
        VK_ACCESS_SHADER_WRITE_BIT | VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED &&
             newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
    params.srcAccessMask = 0;
    params.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    params.dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
                      VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
  } else if (oldLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL &&
             newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
    params.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    params.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    params.srcStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    params.dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
  } else {
    throw std::invalid_argument(
        fmt::format("Unsupported layout transition: {} to {}",
                    static_cast<int>(oldLayout), static_cast<int>(newLayout)));
  }
  return params;
}

bool hasStencilComponent(VkFormat format) {
  return (format == VK_FORMAT_D32_SFLOAT_S8_UINT) ||
         (format == VK_FORMAT_D24_UNORM_S8_UINT);
}

void Image::transitionLayout(VkImageLayout oldLayout, VkImageLayout newLayout) {
  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, oldLayout, newLayout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void Image::copyBufferToImage_def(VkCommandBuffer cmdBuffer, VkBuffer buffer,
                                  uint32_t offset) {
  assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkBufferImageCopy region = {
      .bufferOffset = offset,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource =
          {
              .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
              .mipLevel = 0,
              .baseArrayLayer = 0,
              .layerCount = 1,
          },
      .imageOffset = {0, 0, 0},
      .imageExtent = {static_cast<uint32_t>(w), static_cast<uint32_t>(h), 1}};

  vkCmdCopyBufferToImage(cmdBuffer, buffer, img,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}

void Image::recordCopyFromBuffer(VkCommandBuffer cmdBuffer, VkBuffer buffer,
                                 uint32_t bufferOffset) {
  recordTransitionLayout(cmdBuffer, layout,
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage_def(cmdBuffer, buffer, bufferOffset);
  recordTransitionLayout(cmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
}

void Image::copyFromBuffer(VkBuffer buffer, uint32_t bufferOffset) {
  auto cmd = beginSingleTimeCommands(context);
  recordCopyFromBuffer(cmd, buffer, bufferOffset);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}
} // namespace vkh
