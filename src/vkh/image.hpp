#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include <filesystem>

namespace vkh {
class EngineContext;
constexpr char defaultName[] = "Unnamed image";
struct ImageCreateInfo {
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  VkImageLayout layout = VK_IMAGE_LAYOUT_UNDEFINED;
  VkSampleCountFlagBits samples = VK_SAMPLE_COUNT_1_BIT;
  VkImageAspectFlags aspect = VK_IMAGE_ASPECT_COLOR_BIT;
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
        numSamples{other.numSamples}, aspectMask{other.aspectMask} {
    other.img = VK_NULL_HANDLE;
    other.view = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
    other.layout = VK_IMAGE_LAYOUT_UNDEFINED;
  }

  operator VkImage() { return img; }
  inline const VkImageView getImageView() { return view; }
  ~Image();

  void copyFromBuffer(VkBuffer buffer, uint32_t bufferOffset = 0);
  void recordCopyFromBuffer(VkCommandBuffer cmd, VkBuffer buffer,
                            uint32_t bufferOffset = 0);

  void transitionLayout(VkImageLayout newLayout);

  static uint32_t getFormatSize(VkFormat format);

  VkDescriptorImageInfo getDescriptorInfo(VkSampler sampler) const {
    return VkDescriptorImageInfo{
        .sampler = sampler, .imageView = view, .imageLayout = layout};
  }

  glm::uvec2 size{};
  unsigned int mipLevels = 1;

  void recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout);
  void recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                              VkImageSubresourceRange subresourceRange);

  std::vector<unsigned char> downloadAndSerializeToPNG();
  void downloadPixels(unsigned char *dst, uint32_t mipLevel);

  VkImageView getView() const { return view; }

private:
  void createView();

  void RecordImageBarrier(VkCommandBuffer cmd,
                          VkPipelineStageFlags srcStageMask,
                          VkPipelineStageFlags dstStageMask,
                          VkAccessFlags srcAccessMask,
                          VkAccessFlags dstAccessMask,
                          VkImageLayout newLayout) const;

  void createImageFromData(void *pixels, size_t dataSize, glm::uvec2 size);
  void createImage(EngineContext &context, glm::uvec2 size,
                   VkImageUsageFlags usage);
  struct TransitionParams {
    VkAccessFlags srcAccessMask;
    VkAccessFlags dstAccessMask;
    VkPipelineStageFlags srcStage;
    VkPipelineStageFlags dstStage;
  };
  TransitionParams getTransitionParams(VkImageLayout oldLayout,
                                       VkImageLayout newLayout);
  void setDbgInfo(const char *name);

  EngineContext &context;

  VkImage img = VK_NULL_HANDLE;
  VkImageView view = VK_NULL_HANDLE;
  VkDeviceMemory memory = VK_NULL_HANDLE;
  VkFormat format;
  VkImageLayout layout;
  VkSampleCountFlagBits numSamples = VK_SAMPLE_COUNT_1_BIT;
  VkImageAspectFlags aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
};
} // namespace vkh
