#include "image.hpp"
#include <ktxvulkan.h>

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

#include "buffer.hpp"
#include "debug.hpp"
#include "deviceHelpers.hpp"

namespace vkh {

// ============================== Image (base) ==============================

Image::Image(Image &&other) noexcept
    : context{other.context}, img{other.img}, view{other.view},
      allocation{other.allocation}, format{other.format},
      layout{other.layout}, numSamples{other.numSamples},
      aspectMask{other.aspectMask} {
  mipLevels = other.mipLevels;
  other.img = nullptr;
  other.view = nullptr;
  other.allocation = nullptr;
  other.layout = vk::ImageLayout::eUndefined;
}

Image::~Image() {
  if (view)
    context.vulkan.device.destroyImageView(view, nullptr);
  if (img)
    vmaDestroyImage(context.vulkan.allocator, img, allocation);
}

void Image::recordTransitionLayout(vk::CommandBuffer cmd,
                                   vk::ImageLayout newLayout) {
  if (newLayout == layout)
    return;
  vk::ImageSubresourceRange range = {aspectMask, 0, mipLevels, 0,
                                     getLayerCount()};
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

  case vk::ImageLayout::eTransferSrcOptimal:
    imageMemoryBarrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
    srcStageMask = vk::PipelineStageFlagBits::eTransfer;
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

  case vk::ImageLayout::eTransferSrcOptimal:
    imageMemoryBarrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
    dstStageMask = vk::PipelineStageFlagBits::eTransfer;
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

void Image::recordCopyFromBuffer(vk::CommandBuffer cmd, vk::Buffer buffer,
                                 uint32_t bufferOffset,
                                 uint32_t baseArrayLayer,
                                 uint32_t layerCount) {
  assert(layout == vk::ImageLayout::eTransferDstOptimal);

  // For layerCount > 1 (e.g. a cubemap's 6 faces), the source buffer must
  // hold `layerCount` faces/slices packed back-to-back starting at
  // bufferOffset -- that's what a tightly-packed vk::BufferImageCopy with
  // bufferRowLength=0/bufferImageHeight=0 assumes per layer.
  vk::BufferImageCopy region{
      bufferOffset,
      0,
      0,
      vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0,
                                 baseArrayLayer, layerCount},
      vk::Offset3D{0, 0, 0},
      getExtent()};

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

void Image::downloadPixels(unsigned char *dst, uint32_t mipLevel,
                           uint32_t baseArrayLayer, uint32_t layerCount) {
  if (mipLevel >= mipLevels) {
    throw std::runtime_error("Mip level out of range");
  }

  vk::Extent3D extent = getExtent(mipLevel);
  vk::DeviceSize pixelSize = getFormatSize(format);
  vk::DeviceSize bufferSize = static_cast<vk::DeviceSize>(extent.width) *
                              extent.height * extent.depth * pixelSize *
                              layerCount;

  Buffer<std::byte> stagingBuffer(context,
                                  vk::BufferUsageFlagBits::eTransferDst,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent,
                                  bufferSize);

  vk::CommandBuffer cmd = beginSingleTimeCommands(context);

  vk::ImageSubresourceRange subresourceRange = {vk::ImageAspectFlagBits::eColor,
                                                mipLevel, 1, baseArrayLayer,
                                                layerCount};
  vk::ImageLayout oldLayout = layout;
  recordTransitionLayout(cmd, vk::ImageLayout::eTransferSrcOptimal,
                         subresourceRange);

  vk::BufferImageCopy region = {
      0,
      0,
      0,
      vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, mipLevel,
                                 baseArrayLayer, layerCount},
      {0, 0, 0},
      extent};

  cmd.copyImageToBuffer(img, vk::ImageLayout::eTransferSrcOptimal,
                        stagingBuffer, 1, &region);

  recordTransitionLayout(cmd, oldLayout, subresourceRange);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  stagingBuffer.map();
  std::memcpy(dst, stagingBuffer.getMappedAddr(), bufferSize);
  stagingBuffer.unmap();
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

// ======================= Shared ktx2 loading/upload =======================

Image::KtxLoadResult Image::loadKtx2(EngineContext &context,
                                     const std::filesystem::path &path) {
  ktxTexture2 *texture;
  ktxResult result = ktxTexture2_CreateFromNamedFile(
      path.string().c_str(), KTX_TEXTURE_CREATE_LOAD_IMAGE_DATA_BIT, &texture);
  if (result != KTX_SUCCESS)
    throw std::runtime_error(
        std::format("Failed to create texture from file {}", path.c_str()));

  vk::Format fmt = (vk::Format)ktxTexture2_GetVkFormat(texture);

  if (ktxTexture2_NeedsTranscoding(texture)) {
    ktx_transcode_fmt_e tf;

    VkPhysicalDeviceFeatures deviceFeatures;
    vkGetPhysicalDeviceFeatures(context.vulkan.physicalDevice, &deviceFeatures);
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
      throw std::runtime_error(std::format(
          "Vulkan implementation does not support any available transcode "
          "target (transcoding file {})",
          path.c_str()));
    }

    result = ktxTexture2_TranscodeBasis(texture, tf, 0);
    if (result != KTX_SUCCESS)
      throw std::runtime_error(
          std::format("Failed to transcode file {}", path.c_str()));

    fmt = (vk::Format)ktxTexture2_GetVkFormat(texture);
  }

  // Many GPUs don't support 3-channel 8-bit formats as sampled optimal
  // images, so RGB is always expanded to RGBA here; every other format
  // uploads as-is.
  bool expandRgbToRgba =
      (fmt == vk::Format::eR8G8B8Srgb || fmt == vk::Format::eR8G8B8Unorm);
  vk::Format format =
      expandRgbToRgba
          ? (fmt == vk::Format::eR8G8B8Srgb ? vk::Format::eR8G8B8A8Srgb
                                            : vk::Format::eR8G8B8A8Unorm)
          : fmt;

  ktxTexture *baseTex = ktxTexture(texture);
  uint32_t numFaces = texture->numFaces; // 6 for cubemaps, 1 otherwise
  uint32_t numLevels = texture->numLevels;
  uint32_t srcTexelSize = expandRgbToRgba ? 3 : Image::getFormatSize(format);
  uint32_t dstTexelSize = expandRgbToRgba ? 4 : srcTexelSize;

  KtxLoadResult out{};
  out.format = format;
  out.size = {texture->baseWidth, texture->baseHeight};
  out.numFaces = numFaces;
  out.numLevels = numLevels;

  vk::DeviceSize totalSize = 0;
  for (uint32_t level = 0; level < numLevels; level++) {
    uint32_t w = std::max(1u, out.size.x >> level);
    uint32_t h = std::max(1u, out.size.y >> level);
    for (uint32_t face = 0; face < numFaces; face++) {
      ktx_size_t offset;
      ktxTexture_GetImageOffset(baseTex, level, 0, face, &offset);
      const unsigned char *src = ktxTexture_GetData(baseTex) + offset;

      size_t pixelCount = static_cast<size_t>(w) * h;
      size_t writeOffset = out.uploadData.size();
      out.uploadData.resize(writeOffset + pixelCount * dstTexelSize);
      unsigned char *dst = out.uploadData.data() + writeOffset;

      if (expandRgbToRgba) {
        for (size_t p = 0; p < pixelCount; p++) {
          dst[p * 4 + 0] = src[p * 3 + 0];
          dst[p * 4 + 1] = src[p * 3 + 1];
          dst[p * 4 + 2] = src[p * 3 + 2];
          dst[p * 4 + 3] = 0xFF;
        }
      } else {
        std::memcpy(dst, src, pixelCount * srcTexelSize);
      }

      vk::BufferImageCopy region{};
      region.bufferOffset = totalSize;
      region.imageSubresource = vk::ImageSubresourceLayers{
          vk::ImageAspectFlagBits::eColor, level, face, 1};
      region.imageExtent = vk::Extent3D{w, h, 1};
      out.regions.push_back(region);

      totalSize += pixelCount * dstTexelSize;
    }
  }

  ktxTexture2_Destroy(texture);
  return out;
}

std::pair<vk::Image, VmaAllocation>
Image::createAndUploadKtxImage(EngineContext &context,
                               const KtxLoadResult &data,
                               uint32_t arrayLayers,
                               vk::ImageCreateFlags flags) {
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.format = data.format;
  imageInfo.extent = vk::Extent3D{data.size.x, data.size.y, 1};
  imageInfo.mipLevels = data.numLevels;
  imageInfo.arrayLayers = arrayLayers;
  imageInfo.samples = vk::SampleCountFlagBits::e1;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                    vk::ImageUsageFlagBits::eSampled;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.flags = flags;

  VmaAllocation allocation = nullptr;
  vk::Image img = createImageWithInfo(context, imageInfo, allocation);

  Buffer<std::byte> stagingBuffer(context, vk::BufferUsageFlagBits::eTransferSrc,
                                  vk::MemoryPropertyFlagBits::eHostVisible |
                                      vk::MemoryPropertyFlagBits::eHostCoherent,
                                  data.uploadData.size());
  stagingBuffer.map();
  stagingBuffer.write(const_cast<unsigned char *>(data.uploadData.data()));

  vk::ImageSubresourceRange fullRange{vk::ImageAspectFlagBits::eColor, 0,
                                      data.numLevels, 0, arrayLayers};

  // Transitions/copy are recorded manually here (rather than through an
  // Image instance's recordTransitionLayout) since the vk::Image doesn't
  // belong to a constructed Image object yet - the caller assigns `img`
  // into its own `this->img` right after this returns.
  auto cmd = beginSingleTimeCommands(context);
  vk::ImageMemoryBarrier toDst{};
  toDst.oldLayout = vk::ImageLayout::eUndefined;
  toDst.newLayout = vk::ImageLayout::eTransferDstOptimal;
  toDst.image = img;
  toDst.subresourceRange = fullRange;
  toDst.srcAccessMask = vk::AccessFlagBits::eNone;
  toDst.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTopOfPipe,
                     vk::PipelineStageFlagBits::eTransfer, {}, nullptr,
                     nullptr, toDst);

  cmd.copyBufferToImage(stagingBuffer, img, vk::ImageLayout::eTransferDstOptimal,
                       static_cast<uint32_t>(data.regions.size()),
                       data.regions.data());

  vk::ImageMemoryBarrier toRead{};
  toRead.oldLayout = vk::ImageLayout::eTransferDstOptimal;
  toRead.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  toRead.image = img;
  toRead.subresourceRange = fullRange;
  toRead.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  toRead.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                     vk::PipelineStageFlagBits::eFragmentShader, {}, nullptr,
                     nullptr, toRead);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  return {img, allocation};
}



void Image2D::createView() {
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

void Image2D::createImage(glm::uvec2 imgSize, vk::ImageUsageFlags usage) {
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

  imageInfo.extent.width = imgSize.x;
  imageInfo.extent.height = imgSize.y;
  imageInfo.extent.depth = 1;

  img = createImageWithInfo(context, imageInfo, allocation);
}

void Image2D::createImageFromData(void *pixels, const size_t dataSize,
                                  glm::uvec2 imgSize) {
  if (!pixels) {
    throw std::runtime_error("failed to load texture image from memory!");
  }

  createImage(imgSize,
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

Image2D::Image2D(EngineContext &context, const ImageCreateInfo_PNGdata &createInfo)
    : Image(context) {
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

Image2D::Image2D(EngineContext &context, const ImageCreateInfo_color &createInfo)
    : Image(context) {
  format = createInfo.format;
  size = createInfo.size;
  layout = createInfo.layout;
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(createInfo.size, createInfo.usage);
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

Image2D::Image2D(EngineContext &context, const ImageCreateInfo_empty &createInfo)
    : Image(context) {
  size = createInfo.size;
  format = createInfo.format;
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(size, createInfo.usage);
  createView();

  layout = vk::ImageLayout::eUndefined;
  if (createInfo.layout != vk::ImageLayout::eUndefined) {
    transitionLayout(createInfo.layout);
  }
  setDbgInfo(createInfo.name);
}

Image2D::Image2D(EngineContext &context, const ImageCreateInfo_data &createInfo)
    : Image(context) {
  format = createInfo.format;
  size = createInfo.size;
  layout = createInfo.layout;
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(createInfo.size, createInfo.usage);
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

void Image2D::loadFromKtx2(const std::filesystem::path &path) {
  KtxLoadResult data = Image::loadKtx2(context, path);
  if (data.numFaces == 6)
    throw std::runtime_error(std::format(
        "{} is a cubemap (6 faces) - load it as a Cubemap, not an Image2D",
        path.c_str()));

  format = data.format;
  size = data.size;
  mipLevels = data.numLevels;
  layout = vk::ImageLayout::eUndefined;

  auto [createdImg, createdAlloc] =
      Image::createAndUploadKtxImage(context, data, 1, {});
  img = createdImg;
  allocation = createdAlloc;
  layout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = img;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = format;
  viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels,
                               0, 1};
  if (context.vulkan.device.createImageView(&viewInfo, nullptr, &view) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create image view!");

  setDbgInfo(path.c_str());
}

Image2D::Image2D(EngineContext &context, const std::filesystem::path &path)
    : Image(context) {
  if (path.extension() == ".ktx2") {
    loadFromKtx2(path);
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

std::vector<unsigned char> Image2D::downloadAndSerializeToPNG() {
  if (format != vk::Format::eR8G8B8A8Unorm &&
      format != vk::Format::eR8G8B8A8Srgb) {
    throw std::runtime_error("downloadAndSerializeToPNG only supports "
                             "R8G8B8A8_UNORM or SRGB formats");
  }

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

// =============================== Image3D ===============================

void Image3D::createView() {
  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = img;
  viewInfo.viewType = vk::ImageViewType::e3D;
  viewInfo.format = format;
  viewInfo.subresourceRange = {aspectMask, 0, mipLevels, 0, 1};
  if (context.vulkan.device.createImageView(&viewInfo, nullptr, &view) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create 3D image view!");
  }
}

void Image3D::createImage(glm::uvec3 imgSize, vk::ImageUsageFlags usage) {
  const vk::ImageLayout initLayout = vk::ImageLayout::eUndefined;
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e3D;
  imageInfo.format = format;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 1;
  imageInfo.samples = numSamples;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = usage;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;
  imageInfo.initialLayout = initLayout;
  layout = initLayout;
  imageInfo.extent = vk::Extent3D{imgSize.x, imgSize.y, imgSize.z};

  img = createImageWithInfo(context, imageInfo, allocation);
}

Image3D::Image3D(EngineContext &context, const ImageCreateInfo_empty3D &createInfo)
    : Image(context) {
  size = createInfo.size;
  format = createInfo.format;
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  createImage(size, createInfo.usage);
  createView();

  layout = vk::ImageLayout::eUndefined;
  if (createInfo.layout != vk::ImageLayout::eUndefined) {
    transitionLayout(createInfo.layout);
  }
  setDbgInfo(createInfo.name);
}

// =============================== Cubemap ===============================

Cubemap::Cubemap(EngineContext &context,
                 const ImageCreateInfo_cubemapStorage &createInfo)
    : Image(context) {
  faceSize = createInfo.faceSize;
  format = createInfo.format;
  layout = vk::ImageLayout::eUndefined;
  numSamples = createInfo.samples;
  mipLevels = createInfo.mipLevels;
  aspectMask = createInfo.aspect;

  // Not reusing Image2D's helpers here since those hardcode arrayLayers=1 /
  // a single 2D view - a cubemap needs 6 array layers and the
  // eCubeCompatible flag instead.
  vk::ImageCreateInfo imageInfo{};
  imageInfo.imageType = vk::ImageType::e2D;
  imageInfo.format = format;
  imageInfo.mipLevels = mipLevels;
  imageInfo.arrayLayers = 6;
  imageInfo.samples = numSamples;
  imageInfo.tiling = vk::ImageTiling::eOptimal;
  imageInfo.usage = createInfo.usage;
  imageInfo.sharingMode = vk::SharingMode::eExclusive;
  imageInfo.initialLayout = vk::ImageLayout::eUndefined;
  imageInfo.flags = vk::ImageCreateFlagBits::eCubeCompatible;
  imageInfo.extent.width = createInfo.faceSize;
  imageInfo.extent.height = createInfo.faceSize;
  imageInfo.extent.depth = 1;

  img = createImageWithInfo(context, imageInfo, allocation);

  // 2D-array view (6 layers), for compute imageStore access per face.
  vk::ImageViewCreateInfo arrayViewInfo{};
  arrayViewInfo.image = img;
  arrayViewInfo.viewType = vk::ImageViewType::e2DArray;
  arrayViewInfo.format = format;
  arrayViewInfo.subresourceRange = {aspectMask, 0, mipLevels, 0, 6};
  if (context.vulkan.device.createImageView(&arrayViewInfo, nullptr,
                                            &arrayView) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create cubemap array view!");
  }

  // Cube view, for sampling (this is what view/getDescriptorInfo() use).
  vk::ImageViewCreateInfo cubeViewInfo{};
  cubeViewInfo.image = img;
  cubeViewInfo.viewType = vk::ImageViewType::eCube;
  cubeViewInfo.format = format;
  cubeViewInfo.subresourceRange = {aspectMask, 0, mipLevels, 0, 6};
  if (context.vulkan.device.createImageView(&cubeViewInfo, nullptr, &view) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create cubemap cube view!");
  }

  layout = vk::ImageLayout::eUndefined;
  if (createInfo.layout != vk::ImageLayout::eUndefined) {
    // Covers all 6 layers, unlike the generic transitionLayout()/
    // recordTransitionLayout(cmd,newLayout) overload, which would derive
    // layerCount from getLayerCount() anyway - kept explicit here to match
    // the original behaviour.
    auto cmd = beginSingleTimeCommands(context);
    recordTransitionLayout(
        cmd, createInfo.layout,
        vk::ImageSubresourceRange{aspectMask, 0, mipLevels, 0, 6});
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  }
  setDbgInfo(createInfo.name);
}

Cubemap::Cubemap(EngineContext &context, const std::filesystem::path &path)
    : Image(context) {
  if (path.extension() != ".ktx2")
    throw std::runtime_error(std::format(
        "{} isn't a .ktx2 file - Cubemap only loads ktx2 cube textures",
        path.c_str()));

  Image::KtxLoadResult data = Image::loadKtx2(context, path);
  if (data.numFaces != 6)
    throw std::runtime_error(std::format(
        "{} isn't a cubemap (expected 6 faces, got {}) - load it as an "
        "Image2D instead",
        path.c_str(), data.numFaces));

  format = data.format;
  faceSize = data.size.x;
  mipLevels = data.numLevels;

  auto [createdImg, createdAlloc] = Image::createAndUploadKtxImage(
      context, data, 6, vk::ImageCreateFlagBits::eCubeCompatible);
  img = createdImg;
  allocation = createdAlloc;
  layout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::ImageViewCreateInfo arrayViewInfo{};
  arrayViewInfo.image = img;
  arrayViewInfo.viewType = vk::ImageViewType::e2DArray;
  arrayViewInfo.format = format;
  arrayViewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                                    mipLevels, 0, 6};
  if (context.vulkan.device.createImageView(&arrayViewInfo, nullptr,
                                            &arrayView) != vk::Result::eSuccess)
    throw std::runtime_error("failed to create cubemap array view!");

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = img;
  viewInfo.viewType = vk::ImageViewType::eCube;
  viewInfo.format = format;
  viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, mipLevels,
                               0, 6};
  if (context.vulkan.device.createImageView(&viewInfo, nullptr, &view) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create cubemap cube view!");

  setDbgInfo(path.c_str());
}

Cubemap::~Cubemap() {
  if (arrayView)
    context.vulkan.device.destroyImageView(arrayView, nullptr);
}

// ============================== Image::fromFile ==============================

std::unique_ptr<Image> Image::fromFile(EngineContext &context,
                                       const std::filesystem::path &path) {
  if (path.extension() != ".ktx2")
    return std::make_unique<Image2D>(context, path);

  // Parsed once here (rather than re-delegating to Image2D's/Cubemap's own
  // path constructors, which would each call loadKtx2 independently) since
  // we don't know which type to construct until after parsing.
  KtxLoadResult data = loadKtx2(context, path);

  if (data.numFaces == 6) {
    std::unique_ptr<Cubemap> outImg(new Cubemap(context));
    outImg->format = data.format;
    outImg->faceSize = data.size.x;
    outImg->mipLevels = data.numLevels;

    auto [img, alloc] = createAndUploadKtxImage(
        context, data, 6, vk::ImageCreateFlagBits::eCubeCompatible);
    outImg->img = img;
    outImg->allocation = alloc;
    outImg->layout = vk::ImageLayout::eShaderReadOnlyOptimal;

    vk::ImageViewCreateInfo arrayViewInfo{};
    arrayViewInfo.image = img;
    arrayViewInfo.viewType = vk::ImageViewType::e2DArray;
    arrayViewInfo.format = data.format;
    arrayViewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                                      data.numLevels, 0, 6};
    if (context.vulkan.device.createImageView(&arrayViewInfo, nullptr,
                                              &outImg->arrayView) !=
        vk::Result::eSuccess)
      throw std::runtime_error("failed to create cubemap array view!");

    vk::ImageViewCreateInfo viewInfo{};
    viewInfo.image = img;
    viewInfo.viewType = vk::ImageViewType::eCube;
    viewInfo.format = data.format;
    viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                                 data.numLevels, 0, 6};
    if (context.vulkan.device.createImageView(&viewInfo, nullptr,
                                              &outImg->view) !=
        vk::Result::eSuccess)
      throw std::runtime_error("failed to create cubemap cube view!");

    outImg->setDbgInfo(path.c_str());
    return outImg;
  }

  std::unique_ptr<Image2D> outImg(new Image2D(context));
  outImg->format = data.format;
  outImg->size = data.size;
  outImg->mipLevels = data.numLevels;

  auto [img, alloc] = createAndUploadKtxImage(context, data, 1, {});
  outImg->img = img;
  outImg->allocation = alloc;
  outImg->layout = vk::ImageLayout::eShaderReadOnlyOptimal;

  vk::ImageViewCreateInfo viewInfo{};
  viewInfo.image = img;
  viewInfo.viewType = vk::ImageViewType::e2D;
  viewInfo.format = data.format;
  viewInfo.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0,
                               data.numLevels, 0, 1};
  if (context.vulkan.device.createImageView(&viewInfo, nullptr,
                                            &outImg->view) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create image view!");

  outImg->setDbgInfo(path.c_str());
  return outImg;
}

} // namespace vkh
