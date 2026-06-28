#include "image.hpp"

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

void Image::recordTransitionLayout(vk::CommandBuffer cmd,
                                   vk::ImageLayout newLayout) {
  if (newLayout == layout)
    return;
  vk::ImageSubresourceRange range = {aspectMask, 0, mipLevels, 0, 1};

  recordTransitionLayout(cmd, newLayout, range);
}

void Image::recordTransitionLayout(vk::CommandBuffer cmd,
                                   vk::ImageLayout newLayout,
                                   vk::ImageSubresourceRange subresourceRange) {
  vk::ImageMemoryBarrier imageMemoryBarrier{};
  imageMemoryBarrier.oldLayout = layout;
  imageMemoryBarrier.newLayout = newLayout;
  imageMemoryBarrier.image = img;
  imageMemoryBarrier.subresourceRange = subresourceRange;

  vk::PipelineStageFlags srcStageMask = vk::PipelineStageFlagBits::eNone;
  vk::PipelineStageFlags dstStageMask = vk::PipelineStageFlagBits::eNone;

  switch (layout) {
  case vk::ImageLayout::eUndefined:
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eNone;
    srcStageMask = vk::PipelineStageFlagBits::eTopOfPipe;
    break;

  case vk::ImageLayout::ePreinitialized:
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eHostWrite;
    srcStageMask = vk::PipelineStageFlagBits::eHost;
    break;

  case vk::ImageLayout::eTransferDstOptimal:
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    srcStageMask = vk::PipelineStageFlagBits::eTransfer;
    break;

  case vk::ImageLayout::eGeneral:
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    srcStageMask = vk::PipelineStageFlagBits::eComputeShader;
    break;

  case vk::ImageLayout::eShaderReadOnlyOptimal:
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
    srcStageMask = vk::PipelineStageFlagBits::eVertexShader |
                   vk::PipelineStageFlagBits::eTessellationEvaluationShader |
                   vk::PipelineStageFlagBits::eFragmentShader;
    break;

  case vk::ImageLayout::eColorAttachmentOptimal:
    imageMemoryBarrier.srcAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite;
    srcStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    break;

  case vk::ImageLayout::eDepthStencilAttachmentOptimal:
    imageMemoryBarrier.srcAccessMask =
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    srcStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
                   vk::PipelineStageFlagBits::eLateFragmentTests;
    break;

  default:
    break;
  }

  switch (newLayout) {
  case vk::ImageLayout::eGeneral:
    imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderWrite;
    dstStageMask = vk::PipelineStageFlagBits::eComputeShader;
    break;

  case vk::ImageLayout::eShaderReadOnlyOptimal:
    imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    dstStageMask = vk::PipelineStageFlagBits::eVertexShader |
                   vk::PipelineStageFlagBits::eTessellationEvaluationShader |
                   vk::PipelineStageFlagBits::eFragmentShader;
    break;

  case vk::ImageLayout::eTransferDstOptimal:
    imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    dstStageMask = vk::PipelineStageFlagBits::eTransfer;
    break;

  case vk::ImageLayout::eColorAttachmentOptimal:
    imageMemoryBarrier.dstAccessMask =
        vk::AccessFlagBits::eColorAttachmentWrite |
        vk::AccessFlagBits::eColorAttachmentRead;
    dstStageMask = vk::PipelineStageFlagBits::eColorAttachmentOutput;
    break;

  case vk::ImageLayout::eDepthStencilAttachmentOptimal:
    imageMemoryBarrier.dstAccessMask =
        vk::AccessFlagBits::eDepthStencilAttachmentRead |
        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
    dstStageMask = vk::PipelineStageFlagBits::eEarlyFragmentTests |
                   vk::PipelineStageFlagBits::eLateFragmentTests;
    break;

  default:
    break;
  }

  cmd.pipelineBarrier(srcStageMask, dstStageMask, vk::DependencyFlags{}, 0,
                      nullptr, 0, nullptr, 1, &imageMemoryBarrier);
  layout = newLayout;
}

uint32_t Image::getFormatSize(vk::Format format) {
  switch (format) {
  case vk::Format::eR8Sint:
  case vk::Format::eR8Snorm:
  case vk::Format::eR8Uint:
  case vk::Format::eR8Unorm:
    return 1;
  case vk::Format::eR8G8Sint:
  case vk::Format::eR8G8Snorm:
  case vk::Format::eR8G8Uint:
  case vk::Format::eR8G8Unorm:
  case vk::Format::eR16Sfloat:
  case vk::Format::eR16Sint:
  case vk::Format::eR16Uint:
    return 2;
  case vk::Format::eB8G8R8A8Srgb:
  case vk::Format::eR8G8B8A8Srgb:
  case vk::Format::eB8G8R8A8Unorm:
  case vk::Format::eR8G8B8A8Sint:
  case vk::Format::eR8G8B8A8Snorm:
  case vk::Format::eR8G8B8A8Uint:
  case vk::Format::eR8G8B8A8Unorm:
  case vk::Format::eR16G16Sfloat:
  case vk::Format::eR16G16Sint:
  case vk::Format::eR16G16Uint:
  case vk::Format::eR32Sfloat:
  case vk::Format::eR32Sint:
  case vk::Format::eR32Uint:
    return 4;
  case vk::Format::eR16G16B16A16Sfloat:
  case vk::Format::eR16G16B16A16Sint:
  case vk::Format::eR16G16B16A16Uint:
  case vk::Format::eR32G32Sfloat:
  case vk::Format::eR32G32Sint:
  case vk::Format::eR32G32Uint:
    return 4 * 2;
  case vk::Format::eR32G32B32A32Sfloat:
  case vk::Format::eR32G32B32A32Sint:
  case vk::Format::eR32G32B32A32Uint:
    return 4 * 4;
  default:
    throw std::runtime_error("Unsupported format for formatSize");
  }
}

void Image::transitionLayout(vk::ImageLayout newLayout) {
  if (newLayout == layout)
    return;
  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, newLayout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void Image::createView() {
  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = img;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = format;
  viewInfo.subresourceRange.aspectMask = aspectMask;
  viewInfo.subresourceRange.baseMipLevel = 0;
  viewInfo.subresourceRange.levelCount = mipLevels;
  viewInfo.subresourceRange.baseArrayLayer = 0;
  viewInfo.subresourceRange.layerCount = 1;

  if (context.vulkan.device.createImageView(&viewInfo, nullptr, &view) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create image view!");
  }
}

void Image::createImage(EngineContext &context, glm::uvec2 size,
                        vk::ImageUsageFlags usage) {
  const vk::ImageLayout initLayout = vk::ImageLayout::eUndefined;
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.format = format;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = numSamples;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = usage;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;
  imageInfo.initialLayout = initLayout;
  layout = initLayout;

  imageInfo.extent.width = size.x;
  imageInfo.extent.height = size.y;
  imageInfo.extent.depth = 1;

  if (context.vulkan.device.createImage(&imageInfo, nullptr, &img) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create image!");
  }

  vk::MemoryRequirements memRequirements =
      context.vulkan.device.getImageMemoryRequirements(img);

  vk::MemoryAllocateInfo allocInfo{};
  allocInfo.allocationSize = memRequirements.size;
  allocInfo.memoryTypeIndex =
      findMemoryType(context, memRequirements.memoryTypeBits,
                     vk::MemoryPropertyFlagBits::eDeviceLocal);

  if (context.vulkan.device.allocateMemory(&allocInfo, nullptr, &memory) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  context.vulkan.device.bindImageMemory(img, memory, 0);
}

void Image::createImageFromData(void *pixels, const size_t dataSize,
                                glm::uvec2 size) {
  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory!");
  }

  createImage(context, size,
              vk::ImageUsageFlagBits::eTransferDst |
                  vk::ImageUsageFlagBits::eSampled);

  Buffer<std::byte> stagingBuffer(context,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent,
                                  dataSize);
  stagingBuffer.map();
  stagingBuffer.write(pixels);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, vk::ImageLayout::eTransferDstOptimal);
  recordCopyFromBuffer(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, vk::ImageLayout::eShaderReadOnlyOptimal);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  createView();
}

Image::Image(EngineContext &context, const ImageCreateInfo_PNGdata &createInfo)
    : context{context} {
  format = vk::Format::eR8G8B8A8Srgb;
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
                        "transcode target (transcoding file {})",
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
    format = static_cast<vk::Format>(vkTexture.imageFormat);
    layout = static_cast<vk::ImageLayout>(vkTexture.imageLayout);
    size = {vkTexture.width, vkTexture.height};
    mipLevels = vkTexture.levelCount;

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.viewType = static_cast<vk::ImageViewType>(vkTexture.viewType);
    viewInfo.format = format;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
    viewInfo.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;
    viewInfo.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
    viewInfo.image = img;

    if (context.vulkan.device.createImageView(&viewInfo, nullptr, &view) !=
        vk::Result::eSuccess) {
      throw std::runtime_error("failed to create image view!");
    }

    ktxVulkanDeviceInfo_Destruct(&vdi);
    ktxTexture2_Destroy(texture);
    return;
  }

  format = vk::Format::eR8G8B8A8Unorm;
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
  vk::DeviceSize bufferSize = sizeof(uint32_t);
  Buffer<std::byte> stagingBuffer(context,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent,
                                  bufferSize);
  stagingBuffer.map();
  stagingBuffer.write(data);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, vk::ImageLayout::eTransferDstOptimal);
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

  layout = vk::ImageLayout::eUndefined;
  if (createInfo.layout != vk::ImageLayout::eUndefined) {
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

  vk::DeviceSize bufferSize =
      createInfo.size.x * createInfo.size.y * Image::getFormatSize(format);
  Buffer<std::byte> stagingBuffer(context,
                                  vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent,
                                  bufferSize);
  stagingBuffer.map();
  stagingBuffer.write(createInfo.data);

  auto cmd = beginSingleTimeCommands(context);
  recordTransitionLayout(cmd, vk::ImageLayout::eTransferDstOptimal);
  recordCopyFromBuffer(cmd, stagingBuffer, 0);
  recordTransitionLayout(cmd, createInfo.layout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  setDbgInfo(createInfo.name);
}

Image::~Image() {
  if (view)
    context.vulkan.device.destroyImageView(view, nullptr);
  if (isKtxManaged) {
    ktxVulkanTexture_Destruct(&ktxVkTexture, context.vulkan.device, nullptr);
  } else {
    if (img)
      context.vulkan.device.destroyImage(img, nullptr);
    if (memory)
      context.vulkan.device.freeMemory(memory, nullptr);
  }
}

void Image::recordCopyFromBuffer(vk::CommandBuffer cmd, vk::Buffer buffer,
                                 uint32_t bufferOffset) {
  assert(layout == vk::ImageLayout::eTransferDstOptimal);

  vk::BufferImageCopy region{
      bufferOffset,              // bufferOffset
      0,                         // bufferRowLength
      0,                         // bufferImageHeight
      vk::ImageSubresourceLayers{// imageSubresource
                                 vk::ImageAspectFlagBits::eColor, 0, 0, 1},
      vk::Offset3D{0, 0, 0},          // imageOffset
      vk::Extent3D{size.x, size.y, 1} // imageExtent
  };

  cmd.copyBufferToImage(buffer, img, vk::ImageLayout::eTransferDstOptimal, 1,
                        &region);
}

void Image::copyFromBuffer(vk::Buffer buffer, uint32_t bufferOffset) {
  auto cmd = beginSingleTimeCommands(context);
  vk::ImageLayout oldLayout = layout;
  if (layout != vk::ImageLayout::eTransferDstOptimal)
    recordTransitionLayout(cmd, vk::ImageLayout::eTransferDstOptimal);
  recordCopyFromBuffer(cmd, buffer, bufferOffset);
  if (oldLayout != vk::ImageLayout::eTransferDstOptimal)
    recordTransitionLayout(cmd, oldLayout);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void Image::downloadPixels(unsigned char *dst, uint32_t mipLevel) {
  if (format != vk::Format::eR8G8B8A8Unorm &&
      format != vk::Format::eR8G8B8A8Srgb) {
    throw std::runtime_error(
        "downloadPixels only supports R8G8B8A8_UNORM or SRGB formats");
  }
  if (mipLevel >= mipLevels) {
    throw std::runtime_error("Mip level out of range");
  }

  uint32_t width = size.x >> mipLevel;
  uint32_t height = size.y >> mipLevel;
  vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(width) * height * 4;

  Buffer<std::byte> stagingBuffer(context,
                                  vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent,
                                  bufferSize);

  vk::CommandBuffer cmd = beginSingleTimeCommands(context);

  vk::ImageSubresourceRange subresourceRange = {vk::ImageAspectFlagBits::eColor,
                                                mipLevel, 1, 0, 1};
  vk::ImageLayout oldLayout = layout;
  recordTransitionLayout(cmd, vk::ImageLayout::eTransferSrcOptimal,
                         subresourceRange);

  vk::BufferImageCopy region = {
      0,
      0,
      0,
      vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mipLevel, 0,
                                 1},
      {0, 0, 0},
      {width, height, 1}};

  cmd.copyImageToBuffer(img, vk::ImageLayout::eTransferSrcOptimal,
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
  debug::setObjName(context, vk::ObjectType::eImage,
                    reinterpret_cast<uint64_t>(static_cast<VkImage>(img)),
                    str.c_str());
  str = std::format("image view for image {}", name);
  debug::setObjName(context, vk::ObjectType::eImageView,
                    reinterpret_cast<uint64_t>(static_cast<VkImageView>(view)),
                    str.c_str());
}

} // namespace vkh
