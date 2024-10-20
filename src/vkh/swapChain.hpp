#pragma once

#include "engineContext.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vkh {
namespace swapChain {

constexpr int MAX_FRAMES_IN_FLIGHT = 2;
void create(EngineContext &context);
void cleanup(EngineContext& context);
inline float extentAspectRatio(EngineContext &context) {
  return static_cast<float>(context.vulkan.swapChainExtent.width) /
         static_cast<float>(context.vulkan.swapChainExtent.height);
}
VkResult acquireNextImage(EngineContext &context, uint32_t *imageIndex);
VkResult submitCommandBuffers(EngineContext &context,
                              const VkCommandBuffer *buffers,
                              uint32_t *imageIndex);
inline VkFramebuffer getFrameBuffer(EngineContext &context, int index) {
  return context.vulkan.swapChainFramebuffers[index];
}
/*class LveSwapChain {
public:
  LveSwapChain(EngineContext &context, VkExtent2D windowExtent);
  LveSwapChain(EngineContext &context, VkExtent2D windowExtent,
               std::shared_ptr<LveSwapChain> previous);

  ~LveSwapChain();

  LveSwapChain(const LveSwapChain &) = delete;
  LveSwapChain &operator=(const LveSwapChain &) = delete;

  VkRenderPass getRenderPass() { return renderPass; }
  VkImageView getImageView(int index) { return swapChainImageViews[index]; }
  size_t imageCount() { return swapChainImages.size(); }
  VkFormat getSwapChainImageFormat() { return swapChainImageFormat; }
  VkExtent2D getSwapChainExtent() { return swapChainExtent; }
  uint32_t width() { return swapChainExtent.width; }
  uint32_t height() { return swapChainExtent.height; }

  VkFormat findDepthFormat();


  bool compareSwapFormats(const LveSwapChain &swapChain) const {
    return swapChain.swapChainDepthFormat == swapChainDepthFormat &&
           swapChain.swapChainImageFormat == swapChainImageFormat;
  }

private:
  void init();
  void createSyncObjects();

};*/
} // namespace swapChain
} // namespace vkh
