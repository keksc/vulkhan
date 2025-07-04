#include "image.hpp"
#include <stdexcept>
#include <vulkan/vulkan_core.h>

#define STB_IMAGE_IMPLEMENTATION
#ifdef _WIN32
#include <stb_image.h>
#else
#include <stb/stb_image.h>
#endif
#include <ktx.h>
#include <ktxvulkan.h>

#include "buffer.hpp"
#include "deviceHelpers.hpp"

namespace vkh {
void Image::recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                                   VkImageSubresourceRange subresourceRange) {
  VkImageMemoryBarrier imageMemoryBarrier{
      .sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER};
  imageMemoryBarrier.oldLayout = layout;
  imageMemoryBarrier.newLayout = newLayout;
  imageMemoryBarrier.image = img;
  imageMemoryBarrier.subresourceRange = subresourceRange;

  // Source layouts (old)
  // Source access mask controls actions that have to be finished on the old
  // layout before it will be transitioned to the new layout
  switch (layout) {
  case VK_IMAGE_LAYOUT_UNDEFINED:
    // Image layout is undefined (or does not matter)
    // Only valid as initial layout
    // No flags required, listed only for completeness
    imageMemoryBarrier.srcAccessMask = 0;
    break;

  case VK_IMAGE_LAYOUT_PREINITIALIZED:
    // Image is preinitialized
    // Only valid as initial layout for linear images, preserves memory contents
    // Make sure host writes have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_HOST_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image is a color attachment
    // Make sure any writes to the color buffer have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image is a depth/stencil attachment
    // Make sure any writes to the depth/stencil buffer have been finished
    imageMemoryBarrier.srcAccessMask =
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image is a transfer source
    // Make sure any reads from the image have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image is a transfer destination
    // Make sure any writes to the image have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image is read by a shader
    // Make sure any shader reads from the image have been finished
    imageMemoryBarrier.srcAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  default:
    // Other source layouts aren't handled (yet)
    break;
  }

  // Target layouts (new)
  // Destination access mask controls the dependency for the new image layout
  switch (newLayout) {
  case VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL:
    // Image will be used as a transfer destination
    // Make sure any writes to the image have been finished
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL:
    // Image will be used as a transfer source
    // Make sure any reads from the image have been finished
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    break;

  case VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL:
    // Image will be used as a color attachment
    // Make sure any writes to the color buffer have been finished
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL:
    // Image layout will be used as a depth/stencil attachment
    // Make sure any writes to depth/stencil buffer have been finished
    imageMemoryBarrier.dstAccessMask =
        imageMemoryBarrier.dstAccessMask |
        VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    break;

  case VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL:
    // Image will be read in a shader (sampler, input attachment)
    // Make sure any writes to the image have been finished
    if (imageMemoryBarrier.srcAccessMask == 0) {
      imageMemoryBarrier.srcAccessMask =
          VK_ACCESS_HOST_WRITE_BIT | VK_ACCESS_TRANSFER_WRITE_BIT;
    }
    imageMemoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    break;
  default:
    // Other source layouts aren't handled (yet)
    break;
  }

  // Put barrier inside setup command buffer
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_ALL_COMMANDS_BIT,
                       VK_PIPELINE_STAGE_ALL_COMMANDS_BIT, 0, 0, nullptr, 0,
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
  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, newLayout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}
void Image::createImage(EngineContext &context, int w, int h,
                        VkImageUsageFlags usage) {
  const VkImageLayout initLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = format,
                              .mipLevels = mipLevels,
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
void Image::createImageFromData(void *pixels, const size_t dataSize) {
  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory!");
  }

  createImage(context, w, h,
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

  view = createImageView(context, img, format);
}
Image::Image(EngineContext &context, void *data, size_t dataSize)
    : context{context} {
  format = VK_FORMAT_R8G8B8A8_SRGB;
  int aw, ah, texChannels;
  stbi_uc *pixels = stbi_load_from_memory((const stbi_uc *)data, dataSize, &aw,
                                          &ah, &texChannels, STBI_rgb_alpha);
  w = aw;
  h = ah;
  createImageFromData(pixels, w * h * 4);
  stbi_image_free(pixels);
  // ktxTexture *texture;
  // ktxResult result = ktxTexture_CreateFromMemory(
  //     reinterpret_cast<ktx_uint8_t *>(data),
  //     reinterpret_cast<ktx_size_t>(dataSize),
  //     KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
  // std::println("result = {}", (int)result);
  // assert(result == KTX_SUCCESS);
  // format = ktxTexture_GetVkFormat(texture);
  // w = texture->baseWidth;
  // h = texture->baseHeight;
  // mipLevels = texture->numLevels;
  // ktx_uint8_t *imgData = ktxTexture_GetData(texture);
  // ktx_size_t imgDataSize = ktxTexture_GetDataSize(texture);
  // createImageFromData(imgData, imgDataSize);
  // ktxTexture_Destroy(texture);
}
Image::Image(EngineContext &context, const std::filesystem::path &path)
    : context{context} {
  if (path.extension() == ".ktx2") {
    ktxTexture *texture;
    ktxResult result = ktxTexture_CreateFromNamedFile(
        path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT,
        &texture);
    format = ktxTexture_GetVkFormat(texture);
    w = texture->baseWidth;
    h = texture->baseHeight;
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

    // Create optimal tiled target image
    VkImageCreateInfo imageCreateInfo{.sType =
                                          VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
    imageCreateInfo.imageType = VK_IMAGE_TYPE_2D;
    imageCreateInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    imageCreateInfo.mipLevels = mipLevels;
    imageCreateInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageCreateInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageCreateInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageCreateInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageCreateInfo.extent = {w, h, 1};
    imageCreateInfo.usage =
        VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
    // Cube faces count as array layers in Vulkan
    imageCreateInfo.arrayLayers = 6;
    // This flag is required for cube map images
    imageCreateInfo.flags = VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;

    vkCreateImage(context.vulkan.device, &imageCreateInfo, nullptr, &img);

    vkGetImageMemoryRequirements(context.vulkan.device, img, &memReqs);

    memAllocInfo.allocationSize = memReqs.size;
    memAllocInfo.memoryTypeIndex = findMemoryType(
        context, memReqs.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);

    vkAllocateMemory(context.vulkan.device, &memAllocInfo, nullptr, &memory);
    vkBindImageMemory(context.vulkan.device, img, memory, 0);
    layout = VK_IMAGE_LAYOUT_UNDEFINED;

    VkCommandBuffer cmd = beginSingleTimeCommands(context);

    // Setup buffer copy regions for each face including all of its miplevels
    std::vector<VkBufferImageCopy> bufferCopyRegions;
    uint32_t offset = 0;

    for (uint32_t face = 0; face < 6; face++) {
      for (uint32_t level = 0; level < mipLevels; level++) {
        // Calculate offset into staging buffer for the current mip level and
        // face
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

    // Image barrier for optimal image (target)
    // Set initial layout for all array layers (faces) of the optimal (target)
    // tiled texture
    VkImageSubresourceRange subresourceRange = {};
    subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    subresourceRange.baseMipLevel = 0;
    subresourceRange.levelCount = mipLevels;
    subresourceRange.layerCount = 6;

    recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           subresourceRange);

    // Copy the cube map faces from the staging buffer to the optimal tiled
    // image
    vkCmdCopyBufferToImage(cmd, stagingBuffer, img,
                           VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                           static_cast<uint32_t>(bufferCopyRegions.size()),
                           bufferCopyRegions.data());

    // Change texture image layout to shader read after all faces have been
    // copied
    recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                           subresourceRange);

    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

    // Create image view
    VkImageViewCreateInfo viewInfo{
        .sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
    // Cube map view type
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
    viewInfo.format = VK_FORMAT_R8G8B8A8_UNORM;
    viewInfo.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    // 6 array layers (faces)
    viewInfo.subresourceRange.layerCount = 6;
    // Set number of mip levels
    viewInfo.subresourceRange.levelCount = mipLevels;
    viewInfo.image = img;
    vkCreateImageView(context.vulkan.device, &viewInfo, nullptr, &view);

    // Clean up staging resources
    ktxTexture_Destroy(texture);
    return;
  }
  format = VK_FORMAT_R8G8B8A8_UNORM;
  int aw, ah, texChannels;
  stbi_uc *pixels =
      stbi_load(path.string().c_str(), &aw, &ah, &texChannels, STBI_rgb_alpha);
  w = aw;
  h = ah;
  createImageFromData(pixels, w * h * 4);
  stbi_image_free(pixels);
}
Image::Image(EngineContext &context, uint32_t w, uint32_t h, VkFormat format,
             uint32_t usage, VkImageLayout layout)
    : context{context}, w{w}, h{h}, format{format} {
  createImage(context, w, h, usage);
  view = createImageView(context, img, format);
  transitionLayout(layout);
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
    size = createInfo.w * createInfo.h * Image::getFormatSize(format);
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
  recordTransitionLayout(cmd, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  recordCopyFromBuffer(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, createInfo.layout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}
Image::~Image() {
  vkDestroyImage(context.vulkan.device, img, nullptr);
  vkDestroyImageView(context.vulkan.device, view, nullptr);
  vkFreeMemory(context.vulkan.device, memory, nullptr);
}
void Image::recordCopyFromBuffer(VkCommandBuffer cmd, VkBuffer buffer,
                                 uint32_t bufferOffset) {
  assert(layout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);

  VkBufferImageCopy region = {
      .bufferOffset = bufferOffset,
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

  vkCmdCopyBufferToImage(cmd, buffer, img, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                         1, &region);
}
void Image::copyFromBuffer(VkBuffer buffer, uint32_t bufferOffset) {
  auto cmd = beginSingleTimeCommands(context);
  recordCopyFromBuffer(cmd, buffer, bufferOffset);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}
} // namespace vkh
