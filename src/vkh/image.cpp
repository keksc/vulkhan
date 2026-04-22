#include "image.hpp"

#include <vulkan/vulkan_core.h>
#define STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION
#ifdef _WIN32
#include <stb_image.h>
#include <stb_image_write.h>
#else
#include <stb/stb_image.h>
#include <stb/stb_image_write.h>
#endif
#include <ktx.h>
#include <ktxvulkan.h>

#include "buffer.hpp"
#include "debug.hpp"
#include "deviceHelpers.hpp"

namespace vkh {

void Image::recordTransitionLayout(VkCommandBuffer cmd,
                                   VkImageLayout newLayout) {
  if (newLayout == layout)
    return;
  VkImageSubresourceRange range = {.aspectMask = aspectMask,
                                   .baseMipLevel = 0,
                                   .levelCount = mipLevels,
                                   .baseArrayLayer = 0,
                                   .layerCount = 1};
  recordTransitionLayout(cmd, newLayout, range);
}

void Image::recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                                   VkImageSubresourceRange subresourceRange) {
  VkImageMemoryBarrier imageMemoryBarrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  imageMemoryBarrier.oldLayout = layout;
  imageMemoryBarrier.newLayout = newLayout;
  imageMemoryBarrier.image = img;
  imageMemoryBarrier.subresourceRange = subresourceRange;
  VkPipelineStageFlags srcStageMask = 0;
  VkPipelineStageFlags dstStageMask = 0;

  switch (layout) {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    imageMemoryBarrier.srcAccessMask = 0;
    srcStageMask = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    break;

  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    srcStageMask = VK_PIPELINE_STAGE_HOST_BIT;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    break;

  case VK_IMAGE_LAYOUT_GENERAL:
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    srcStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    break;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    srcStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                   VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    break;

  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    break;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    imageMemoryBarrier.srcAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    srcStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    break;

  default:
    break;
  }

  switch (newLayout) {
  case VK_IMAGE_LAYOUT_GENERAL:
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    break;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    dstStageMask = VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
                   VK_PIPELINE_STAGE_TESSELLATION_EVALUATION_SHADER_BIT |
                   VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    break;

  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                       VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
    dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    break;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    imageMemoryBarrier.dstAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    dstStageMask = VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT |
                   VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    break;

  default:
    break;
  }

  vkCmdPipelineBarrier(cmd, srcStageMask, dstStageMask, 0, 0, nullptr, 0,
                       nullptr, 1, &imageMemoryBarrier);
  layout = newLayout;
}

uint32_t Image::getFormatSize(VkFormat format) {
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

void Image::transitionLayout(VkImageLayout newLayout) {
  if (newLayout == layout)
    return;
  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, newLayout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void Image::createView() {
  VkImageViewCreateInfo viewInfo{.sType =
                                     VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
  viewInfo.image = img;
  viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (vkCreateImageView(context.vulkan.device, &viewInfo, nullptr, &view) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create image view!");
  }
}

void Image::createImage(EngineContext &context, glm::uvec2 size,
                        VkImageUsageFlags usage) {
  const VkImageLayout initLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .mipLevels = mipLevels,
                              .arrayLayers = 1,
                              .samples = numSamples,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = usage,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = initLayout};
  layout = initLayout;

  imageInfo.extent.width = size.x;
  imageInfo.extent.height = size.y;
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

void Image::createImageFromData(void *pixels, const size_t dataSize,
                                glm::uvec2 size) {
  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory!");
  }

  createImage(context, size,
              VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT);

  Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  dataSize);
  stagingBuffer.map();
  stagingBuffer.write(pixels);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  recordCopyFromBuffer(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  createView();
}

Image::Image(EngineContext &context, const ImageCreateInfo_PNGdata &createInfo)
    : context{context} {
  format = VK_FORMAT_R8G8B8A8_SRGB;
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  int w, h, texChannels;
  stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)createInfo.data,
                                          createInfo.dataSize, &w, &h,
                                          &texChannels, STBI_rgb_alpha);
  size.x = static_cast<unsigned int>(w);
  size.y = static_cast<unsigned int>(h);
  createImageFromData(pixels, size.x * size.y * 4, size);
  stbi_image_free(pixels);

  setDbgInfo(createInfo.name);
}

Image::Image(EngineContext &context, const std::filesystem::path &path)
    : context{context} {
  if (path.extension() == ".ktx2") {
    ktxTexture *texture;
    ktxResult result = ktxTexture_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &texture);
    format = ktxTexture_GetVkFormat(texture);
    size = {texture->baseWidth, texture->baseHeight};
    mipLevels = texture->numLevels;
    ktx_uint8_t *ktxData = ktxTexture_GetData(texture);
    ktx_size_t ktxDataSize = ktxTexture_GetDataSize(texture);
    VkMemoryAllocateInfo memAllocInfo{};
    memAllocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    VkMemoryRequirements memReqs;

    Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    ktxDataSize);

    stagingBuffer.map();
    stagingBuffer.write(ktxData);
    stagingBuffer.unmap();

    VkImageCreateInfo imageCreateInfo{.sType =
                                          VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = format;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = {size.x, size.y, 1};
    imageCreateInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    imageCreateInfo.arrayLayers = 6;
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    if (vkCreateImage(context.vulkan.device, &imageCreateInfo, nullptr, &img) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create KTX image!");
    }

    vkGetImageMemoryRequirements(context.vulkan.device, img, &memReqs);

    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = findMemoryType(
        context, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    if (vkAllocateMemory(context.vulkan.device, &memAllocInfo, nullptr,
                         &memory) != VK_SUCCESS) {
      throw std::runtime_error("failed to allocate KTX image memory!");
    }

    if (vkBindImageMemory(context.vulkan.device, img, memory, 0) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to bind KTX image memory!");
    }

    VkCommandBuffer cmd = beginSingleTimeCommands(context);

    std::vector<VkBufferImageCopy> bufferCopyRegions;
    for (uint32_t face = 0; face < 6; face++) {
      for (uint32_t level = 0; level < mipLevels; level++) {
        ktx_size_t offset;
        KTX_error_code ret =
            ktxTexture_GetImageOffset(texture, level, 0, face, &offset);
        assert(ret == KTX_SUCCESS);
        bufferCopyRegions.emplace_back(
            offset, 0, 0,
            VkImageSubresourceLayers{.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                     .mipLevel = level,
                                     .baseArrayLayer = face,
                                     .layerCount = 1},
            VkOffset3D{},
            VkExtent3D{texture->baseWidth >> level,
                       texture->baseHeight >> level, 1});
      }
    }

    VkImageSubresourceRange subresourceRange{};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = mipLevels;
    subresourceRange.layerCount = 6;

    recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           subresourceRange);

    vkCmdCopyBufferToImage(cmd, stagingBuffer, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(bufferCopyRegions.size()),
                           bufferCopyRegions.data());

    recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           subresourceRange);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    viewInfo.subresourceRange.layerCount = 6;
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.image = img;
    if (vkCreateImageView(context.vulkan.device, &viewInfo, nullptr, &view) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create KTX image view!");
    }

    ktxTexture_Destroy(texture);
    return;
  }

  format = VK_FORMAT_R8G8B8A8_UNORM;
  int w, h, texChannels;
  stbi_uc *pixels =
      stbi_load(path.string().c_str(), &w, &h, &texChannels, STBI_rgb_alpha);
  size.x = static_cast<unsigned int>(w);
  size.y = static_cast<unsigned int>(h);
  createImageFromData(pixels, size.x * size.y * 4, size);
  stbi_image_free(pixels);
  setDbgInfo(path.c_str());
}

Image::Image(EngineContext &context, const ImageCreateInfo_color &createInfo)
    : context{context}, format{createInfo.format}, size{createInfo.size},
      layout{createInfo.layout} {
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(context, createInfo.size, createInfo.usage);
  createView();

  uint32_t color = createInfo.color;
  void *data = static_cast<void *>(&color);
  VkDeviceSize size = sizeof(uint32_t);
  Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  size);
  stagingBuffer.map();
  stagingBuffer.write(data);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  recordCopyFromBuffer(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, createInfo.layout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  setDbgInfo(createInfo.name);
}

Image::Image(EngineContext &context, const ImageCreateInfo_empty &createInfo)
    : context{context}, size{createInfo.size}, format{createInfo.format} {
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(context, size, createInfo.usage);
  createView();

  layout = VK_IMAGE_LAYOUT_UNDEFINED;
  if (createInfo.layout != VK_IMAGE_LAYOUT_UNDEFINED) {
    transitionLayout(createInfo.layout);
  }
  setDbgInfo(createInfo.name);
}

Image::Image(EngineContext &context, const ImageCreateInfo_data &createInfo)
    : context{context}, format{createInfo.format}, size{createInfo.size},
      layout{createInfo.layout} {
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(context, createInfo.size, createInfo.usage);
  createView();

  VkDeviceSize size =
      createInfo.size.x * createInfo.size.y * Image::getFormatSize(format);
  Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  size);
  stagingBuffer.map();
  stagingBuffer.write(createInfo.data);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  recordCopyFromBuffer(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, createInfo.layout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  setDbgInfo(createInfo.name);
}

Image::~Image() {
  if (view != VK_NULL_HANDLE)
    vkDestroyImageView(context.vulkan.device, view, nullptr);
  if (img != VK_NULL_HANDLE)
    vkDestroyImage(context.vulkan.device, img, nullptr);
  if (memory != VK_NULL_HANDLE)
    vkFreeMemory(context.vulkan.device, memory, nullptr);
}

void Image::recordCopyFromBuffer(VkCommandBuffer cmd, VkBuffer buffer,
                                 uint32_t bufferOffset) {
  assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkBufferImageCopy region = {.bufferOffset = bufferOffset,
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
                              .imageExtent = {size.x, size.y, 1}};

  vkCmdCopyBufferToImage(cmd, buffer, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);
}

void Image::copyFromBuffer(VkBuffer buffer, uint32_t bufferOffset) {
  auto cmd = beginSingleTimeCommands(context);
  VkImageLayout oldLayout = layout;
  if (layout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  recordCopyFromBuffer(cmd, buffer, bufferOffset);
  if (oldLayout != VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL)
    recordTransitionLayout(cmd, oldLayout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void Image::downloadPixels(unsigned char *dst, uint32_t mipLevel) {
  if (format != VK_FORMAT_R8G8B8A8_UNORM && format != VK_FORMAT_R8G8B8A8_SRGB) {
    throw std::runtime_error(
        "downloadPixels only supports R8G8B8A8_UNORM or SRGB formats");
  }
  if (mipLevel >= mipLevels) {
    throw std::runtime_error("Mip level out of range");
  }

  uint32_t width = size.x >> mipLevel;
  uint32_t height = size.y >> mipLevel;
  VkDeviceSize bufferSize = static_cast<VkDeviceSize>(width) * height * 4;

  Buffer<std::byte> stagingBuffer(context, VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                                  VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                      VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                  bufferSize);

  VkCommandBuffer cmd = beginSingleTimeCommands(context);

  VkImageSubresourceRange subresourceRange = {.aspectMask =
                                                  VK_IMAGE_ASPECT_COLOR_BIT,
                                              .baseMipLevel = mipLevel,
                                              .levelCount = 1,
                                              .baseArrayLayer = 0,
                                              .layerCount = 1};
  VkImageLayout oldLayout = layout;
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         subresourceRange);

  VkBufferImageCopy region = {
      .bufferOffset = 0,
      .bufferRowLength = 0,
      .bufferImageHeight = 0,
      .imageSubresource = {.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                           .mipLevel = mipLevel,
                           .baseArrayLayer = 0,
                           .layerCount = 1},
      .imageOffset = {0, 0, 0},
      .imageExtent = {width, height, 1}};

  vkCmdCopyImageToBuffer(cmd, img, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
                         stagingBuffer, 1, &region);

  recordTransitionLayout(cmd, oldLayout, subresourceRange);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  stagingBuffer.map();
  std::memcpy(dst, stagingBuffer.getMappedAddr(), bufferSize);
  stagingBuffer.unmap();
}

std::vector<unsigned char> Image::downloadAndSerializeToPNG() {
  std::vector<unsigned char> pixels(size.x * size.y * 4);
  downloadPixels(pixels.data(), 0);

  int len;
  unsigned char *pngData = stbi_write_png_to_mem(
      pixels.data(), static_cast<int>(size.x * 4), static_cast<int>(size.x),
      static_cast<int>(size.y), 4, &len);

  if (!pngData) {
    throw std::runtime_error("Failed to serialize to PNG: " +
                             std::string(stbi_failure_reason()));
  }

  std::vector<uint8_t> result(pngData, pngData + len);
  STBIW_FREE(pngData);
  return result;
}
void Image::setDbgInfo(const char *name) {
  std::string str = std::format("{} image", name);
  debug::setObjName(context, VK_OBJECT_TYPE_IMAGE,
                    reinterpret_cast<uint64_t>(img), str.c_str());
  str = std::format("image view for image {}", name);
  debug::setObjName(context, VK_OBJECT_TYPE_IMAGE_VIEW,
                    reinterpret_cast<uint64_t>(view), str.c_str());
}
} // namespace vkh
