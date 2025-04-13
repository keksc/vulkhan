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
  VkDeviceSize imageSize = w * h * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory !");
  }

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

  auto cmd = beginSingleTimeCommands(context);
  TransitionLayout_UNDEFtoDST_OPTIMAL(cmd);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  copyBufferToImage(context, stagingBuffer, img, static_cast<uint32_t>(w),
                    static_cast<uint32_t>(w));
  cmd = beginSingleTimeCommands(context);
  TransitionLayout_DST_OPTIMALtoSHADER_READ(cmd);
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
Image::Image(EngineContext &context, const ImageCreateInfo &createInfo)
    : context{context}, format{createInfo.format}, w{createInfo.w},
      h{createInfo.h}, layout(createInfo.layout) {
  createImage(context, createInfo.w, createInfo.h, createInfo.usage);
  view = createImageView(context, img, format);
  if (!createInfo.data && !createInfo.color.has_value()) {
    transitionLayout(VK_IMAGE_LAYOUT_UNDEFINED, createInfo.layout);
    return;
  }

  void *data;
  VkDeviceSize size;
  if (createInfo.data) {
    data = createInfo.data;
    size = createInfo.w * createInfo.h * Image::formatSize(format);
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
  copyBufferToImage(context, stagingBuffer, img, createInfo.w, createInfo.h);
  transitionLayout(VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, createInfo.layout);
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
  barrier.image = img;
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

  endSingleTimeCommands(context, commandBuffer, context.vulkan.graphicsQueue);
}
void Image::RecordImageBarrier(VkCommandBuffer cmdBuffer,
                               VkPipelineStageFlags srcStageMask,
                               VkPipelineStageFlags dstStageMask,
                               VkAccessFlags srcAccessMask,
                               VkAccessFlags dstAccessMask,
                               VkImageLayout newLayout) const {
  assert(cmdBuffer != VK_NULL_HANDLE);

  VkImageMemoryBarrier memoryBarrier = {
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
      .pNext = nullptr,
      .srcAccessMask = srcAccessMask,
      // All writes must be AVAILABLE before layout change
      .dstAccessMask = dstAccessMask,
      // >> Layout transition
      .oldLayout = layout,
      .newLayout = newLayout,
      // Not transferring queue family ownership
      .srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED,
      .image = img,
      .subresourceRange = {// Transition the whole image at once
                           .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .baseMipLevel = 0,
                           .levelCount = 1,
                           .baseArrayLayer = 0,
                           .layerCount = 1}};

  vkCmdPipelineBarrier(cmdBuffer, srcStageMask, dstStageMask,
                       0,                // dependencyFlags
                       0, nullptr,       // memory barriers
                       0, nullptr,       // buffer memory barriers
                       1, &memoryBarrier // image memory barriers
  );
}
void Image::TransitionLayout_DST_OPTIMALtoSHADER_READ(
    VkCommandBuffer cmdBuffer, VkPipelineStageFlags dstStage,
    VkAccessFlags dstAccessMask) {
  assert(img != VK_NULL_HANDLE &&
         layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  RecordImageBarrier(cmdBuffer,
                     // srcStageMask,                  dstStageMask
                     VK_PIPELINE_STAGE_TRANSFER_BIT, dstStage,
                     // wait for writes, before reads
                     // srcAccessMask, dstAccessMask
                     VK_ACCESS_TRANSFER_WRITE_BIT, dstAccessMask,
                     // All writes must be AVAILABLE before layout change
                     // >> Layout transition
                     VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
}
void Image::TransitionLayout_SHADER_READtoDST_OPTIMAL(
    VkCommandBuffer cmdBuffer, VkPipelineStageFlags srcStage) {
  assert(img != VK_NULL_HANDLE &&
         layout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  RecordImageBarrier(cmdBuffer,
                     // srcStageMask,                  dstStageMask
                     srcStage, VK_PIPELINE_STAGE_TRANSFER_BIT,
                     // wait for writes, before reads
                     // srcAccessMask, dstAccessMask
                     0, VK_ACCESS_TRANSFER_WRITE_BIT,
                     // All writes must be AVAILABLE before layout change
                     // >> Layout transition
                     VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}
void Image::TransitionLayout_UNDEFtoDST_OPTIMAL(VkCommandBuffer cmdBuffer) {
  assert(img != VK_NULL_HANDLE && layout == VK_IMAGE_LAYOUT_UNDEFINED);

  RecordImageBarrier(
      cmdBuffer,
      // srcStageMask,                  dstStageMask
      VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT,
      // here, writes don't have to wait on anything, but must be completed
      //  before transfer stages can start
      // srcAccessMask, dstAccessMask
      0, VK_ACCESS_TRANSFER_WRITE_BIT,
      // All writes must be AVAILABLE before layout change
      // >> Layout transition
      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  layout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
}
void Image::TransitionLayout(VkCommandBuffer cmdBuffer, VkImageLayout newLayout,
                             VkPipelineStageFlags srcStageMask,
                             VkPipelineStageFlags dstStageMask) {
  VkImageLayout oldLayout = layout;
  VkAccessFlags srcAccessMask = 0;

  // Source image layouts
  // Source access mask controls actions that have to be finished on the old
  // layout before it will be transitioned to the new layout
  switch (oldLayout) {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    // Image layout is undefined, only valid as initial layout
    // No flags required
    srcAccessMask = 0;
    break;
  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    // Only valid as initial layout for linear images, preserves memory contents
    // Host writes must be finished
    srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Writes to the color buffer must be finished
    srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Any writes to the depth/stencil buffer must be finished
    srcAccessMask = VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Any reads from the image must be finished
    srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Any writes to the image must be finished
    srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Any shader reads from the image must be finished
    srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
  default:
    break;
  }

  // Transition to new layout
  // Destination access mask controls the dependency for the new layout
  VkAccessFlags dstAccessMask = 0;

  switch (newLayout) {
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;
  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    dstAccessMask |= VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    if (srcAccessMask == 0) {
      srcAccessMask = VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  default:
    break;
  }

  RecordImageBarrier(cmdBuffer, srcStageMask, dstStageMask, srcAccessMask,
                     dstAccessMask, newLayout);

  layout = newLayout;
}
void Image::TransitionLayoutToDST_OPTIMAL(VkCommandBuffer cmdBuffer,
                                          VkPipelineStageFlags stage) {
  switch (layout) {
  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    TransitionLayout_SHADER_READtoDST_OPTIMAL(cmdBuffer, stage);
    break;
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    return;
  case VK_IMAGE_LAYOUT_UNDEFINED:
    TransitionLayout_UNDEFtoDST_OPTIMAL(cmdBuffer);
    break;
  default:
    TransitionLayout(cmdBuffer, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                     VK_PIPELINE_STAGE_ALL_COMMANDS_BIT);
    // VKP_ASSERT_MSG(false, "Unsupported layout transition");
    break;
  }
}
void Image::copyBufferToImage_def(VkCommandBuffer cmdBuffer, VkBuffer buffer,
                                  uint32_t offset) {
  assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  // Buffer data at offset 0, tightly-packed to whole image
  VkBufferImageCopy region = {
      .bufferOffset = offset,
      // Tightly packed data
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
                         // Assumes the image has been transitioned to optimal
                         VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
}
void Image::copyFromBuffer(VkCommandBuffer cmdBuffer, VkBuffer buffer,
                           bool genMips, VkPipelineStageFlags dstStage,
                           uint32_t bufferOffset, VkAccessFlags dstAccessMask) {
  auto cmd = beginSingleTimeCommands(context);
  TransitionLayoutToDST_OPTIMAL(cmd, dstStage);

  copyBufferToImage_def(cmd, buffer, bufferOffset);

  TransitionLayout_DST_OPTIMALtoSHADER_READ(cmd, dstStage, dstAccessMask);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}
} // namespace vkh
