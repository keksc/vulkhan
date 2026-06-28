#pragma once

#include <vulkan/vulkan.hpp>

#include <glm/glm.hpp>
#include <ktx.h>
#include <ktxvulkan.h>

#include <filesystem>
#include <vector>

namespace vkh {

class EngineContext;
constexpr char defaultName[] = "Unnamed image";

struct ImageCreateInfo {
  vk::Format format = vk::Format::eR8G8B8A8Srgb;
  vk::ImageUsageFlags usage =
      vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
  vk::ImageLayout layout = vk::ImageLayout::eUndefined;
  vk::SampleCountFlagBits samples = vk::SampleCountFlagBits::e1;
  vk::ImageAspectFlags aspect = vk::ImageAspectFlagBits::eColor;
  uint32_t mipLevels = 1;
  const char *name = defaultName;
};

struct ImageCreateInfo_empty : public ImageCreateInfo {
  glm::uvec2 size{};
};

struct ImageCreateInfo_PNGdata : public ImageCreateInfo {
  void *data = nullptr;
  size_t dataSize;
};

struct ImageCreateInfo_data : public ImageCreateInfo {
  void *data = nullptr;
  glm::uvec2 size{};
};

struct ImageCreateInfo_color : public ImageCreateInfo {
  uint32_t color;
  glm::uvec2 size{};
};

class Image {
public:
  Image(EngineContext &context, const ImageCreateInfo_PNGdata &createInfo);
  Image(EngineContext &context, const ImageCreateInfo_data &createInfo);
  Image(EngineContext &context, const ImageCreateInfo_color &createInfo);
  Image(EngineContext &context, const ImageCreateInfo_empty &createInfo);
  Image(EngineContext &context, const std::filesystem::path &path);

  Image(Image &&other) noexcept
      : context{other.context}, img{other.img}, view{other.view},
        memory{other.memory}, format{other.format}, layout{other.layout},
        size{other.size}, mipLevels{other.mipLevels},
        numSamples{other.numSamples}, aspectMask{other.aspectMask},
        ktxVkTexture{other.ktxVkTexture}, isKtxManaged{other.isKtxManaged} {
    other.img = nullptr;
    other.view = nullptr;
    other.memory = nullptr;
    other.layout = vk::ImageLayout::eUndefined;
    other.isKtxManaged = false;
  }

  Image(const Image &) = delete;
  Image &operator=(const Image &) = delete;
  Image &operator=(Image &&) = delete;

  operator vk::Image() { return img; }
  inline const vk::ImageView getImageView() { return view; }
  ~Image();

  void copyFromBuffer(vk::Buffer buffer, uint32_t bufferOffset = 0);
  void recordCopyFromBuffer(vk::CommandBuffer cmd, vk::Buffer buffer,
                            uint32_t bufferOffset = 0);

  void transitionLayout(vk::ImageLayout newLayout);

  static uint32_t getFormatSize(vk::Format format);

  vk::DescriptorImageInfo getDescriptorInfo(vk::Sampler sampler) const {
    return vk::DescriptorImageInfo(sampler, view, layout);
  }

  glm::uvec2 size{};
  unsigned int mipLevels = 1;

  void recordTransitionLayout(vk::CommandBuffer cmd, vk::ImageLayout newLayout);
  void recordTransitionLayout(vk::CommandBuffer cmd, vk::ImageLayout newLayout,
                              vk::ImageSubresourceRange subresourceRange);

  std::vector<unsigned char> downloadAndSerializeToPNG();
  void downloadPixels(unsigned char *dst, uint32_t mipLevel);

  vk::ImageView getView() const { return view; }

private:
  void createView();

  void RecordImageBarrier(vk::CommandBuffer cmd,
                          vk::PipelineStageFlags srcStageMask,
                          vk::PipelineStageFlags dstStageMask,
                          vk::AccessFlags srcAccessMask,
                          vk::AccessFlags dstAccessMask,
                          vk::ImageLayout newLayout) const;

  void createImageFromData(void *pixels, size_t dataSize, glm::uvec2 size);
  void createImage(EngineContext &context, glm::uvec2 size,
                   vk::ImageUsageFlags usage);

  struct TransitionParams {
    vk::AccessFlags srcAccessMask;
    vk::AccessFlags dstAccessMask;
    vk::PipelineStageFlags srcStage;
    vk::PipelineStageFlags dstStage;
  };
  TransitionParams getTransitionParams(vk::ImageLayout oldLayout,
                                       vk::ImageLayout newLayout);
  void setDbgInfo(const char *name);

  EngineContext &context;

  vk::Image img = nullptr;
  vk::ImageView view = nullptr;
  vk::DeviceMemory memory = nullptr;
  vk::Format format;
  vk::ImageLayout layout;
  vk::SampleCountFlagBits numSamples = vk::SampleCountFlagBits::e1;
  vk::ImageAspectFlags aspectMask = vk::ImageAspectFlagBits::eColor;

  ktxVulkanTexture ktxVkTexture = {};
  bool isKtxManaged = false;
};

} // namespace vkh
