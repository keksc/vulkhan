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
    ktxTexture2 *texture;
    ktxResult result = ktxTexture2_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &texture);
    if (result != KTX_SUCCESS)
      throw std::runtime_error(
          std::format("Failed to create texture from file {}", path.c_str()));

    if (ktxTexture2_NeedsTranscoding(texture)) {
      ktx_transcode_fmt_e tf;

      // Using VkGetPhysicalDeviceFeatures or GL_COMPRESSED_TEXTURE_FORMATS or
      // extension queries, determine what compressed texture formats are
      // supported and pick a format. For example
      VkPhysicalDeviceFeatures deviceFeatures;
      vkGetPhysicalDeviceFeatures(context.vulkan.physicalDevice,
                                  &deviceFeatures);
      khr_df_model_e colorModel = ktxTexture2_GetColorModel_e(texture);
      if (colorModel == KHR_DF_MODEL_UASTC &&
          deviceFeatures.textureCompressionASTC_LDR) {
        tf = KTX_TTF_ASTC_4x4_RGBA;
      } else if (colorModel == KHR_DF_MODEL_UASTC &&
                 deviceFeatures.textureCompressionBC) {
        tf = KTX_TTF_BC7_RGBA;
      } else if (colorModel == KHR_DF_MODEL_ETC1S &&
                 deviceFeatures.textureCompressionETC2) {
        tf = KTX_TTF_ETC;
      } else if (deviceFeatures.textureCompressionASTC_LDR) {
        tf = KTX_TTF_ASTC_4x4_RGBA;
      } else if (deviceFeatures.textureCompressionETC2)
        tf = KTX_TTF_ETC2_RGBA;
      else if (deviceFeatures.textureCompressionBC)
        tf = KTX_TTF_BC3_RGBA;
      else {
        throw std::runtime_error(
            std::format("Vulkan implementation does not support any available "
                        "transcode targed (transcoding file {})",
                        path.c_str()));
      }

      result = ktxTexture2_TranscodeBasis(texture, tf, 0);

      if (result != KTX_SUCCESS)
        throw std::runtime_error(
            std::format("Failed to transcode file {}", path.c_str()));
    }
    ktxVulkanDeviceInfo vdi;
    ktxVulkanDeviceInfo_Construct(
        &vdi, context.vulkan.physicalDevice, context.vulkan.device,
        context.vulkan.graphicsQueue, context.vulkan.commandPool, nullptr);
    ktxVulkanTexture vkTexture;
    result = ktxTexture2_VkUploadEx(
        texture, &vdi, &vkTexture, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    if (result != KTX_SUCCESS) {
      ktxVulkanDeviceInfo_Destruct(&vdi);
      ktxTexture2_Destroy(texture);
      throw std::runtime_error("failed to upload KTX image to Vulkan!");
    }
    ktxVkTexture = vkTexture;
    isKtxManaged = true;
    img = vkTexture.image;
    memory = vkTexture.deviceMemory;
    format = vkTexture.imageFormat;
    layout = vkTexture.imageLayout;
    size = {vkTexture.width, vkTexture.height};
    mipLevels = vkTexture.levelCount;

    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    viewInfo.viewType = vkTexture.viewType;
    viewInfo.format = format;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    viewInfo.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    viewInfo.image = img;

    if (vkCreateImageView(context.vulkan.device, &viewInfo, nullptr, &view) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create image view!");
    }

    ktxVulkanDeviceInfo_Destruct(&vdi);
    ktxTexture2_Destroy(texture);
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
  if (isKtxManaged) {
    ktxVulkanTexture_Destruct(&ktxVkTexture, context.vulkan.device, nullptr);
  } else {
    if (img != VK_NULL_HANDLE)
      vkDestroyImage(context.vulkan.device, img, nullptr);
    if (memory != VK_NULL_HANDLE)
      vkFreeMemory(context.vulkan.device, memory, nullptr);
  }
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
