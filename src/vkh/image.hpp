#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include <filesystem>

#include "engineContext.hpp"

namespace vkh {
struct ImageCreateInfo {
  VkFormat format = VK_FORMAT_R8G8B8A8_SRGB;
  VkImageUsageFlags usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_SAMPLED_BIT;
  glm::uvec2 size{};
  void *data = nullptr;
  uint32_t color;
  VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
};
class Image {
public:
  Image(EngineContext &context, glm::uvec2 size, VkFormat format,
        uint32_t usage, VkImageLayout layout);
  Image(EngineContext &context, const ImageCreateInfo &createInfo);
  Image(EngineContext &context, const std::filesystem::path &path);
  Image(EngineContext &context, void *data, size_t dataSize);
  Image(Image &&other) noexcept
      : context{other.context}, img{other.img}, view{other.view},
        memory{other.memory}, format{other.format}, layout{other.layout} {
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

  void recordTransitionLayout(VkCommandBuffer cmd, VkImageLayout newLayout,
                              VkImageSubresourceRange subresourceRange = {
                                  .aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
                                  .baseMipLevel = 0,
                                  .levelCount = 1,
                                  .baseArrayLayer = 0,
                                  .layerCount = 1});

  std::vector<unsigned char> downloadAndSerializeToPNG();
  void downloadPixels(unsigned char *dst, uint32_t mipLevel);

private:
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

  EngineContext &context;

  VkImage img;
  VkImageView view;
  VkDeviceMemory memory;
  VkFormat format;
  VkImageLayout layout;
};
} // namespace vkh
