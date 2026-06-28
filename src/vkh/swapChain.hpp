#pragma once

#include <vulkan/vulkan.hpp>

#include "engineContext.hpp"
#include "image.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vkh {

class SwapChain {
public:
  SwapChain(EngineContext &context);
  SwapChain(EngineContext &context, std::shared_ptr<SwapChain> previous);

  ~SwapChain();

  SwapChain(const SwapChain &) = delete;
  SwapChain &operator=(const SwapChain &) = delete;

  vk::Framebuffer getFrameBuffer(int index) {
    return swapChainFramebuffers[index];
  }
  vk::RenderPass getRenderPass() { return renderPass; }
  vk::ImageView getImageView(size_t index) {
    return swapChainImageViews[index];
  }
  vk::ImageView getDepthImageView(size_t index) {
    return depthImages[index].getImageView();
  }
  vk::Image getImage(size_t index) const { return swapChainImages[index]; }
  vk::Image getDepthImage(size_t index) { return depthImages[index]; }
  size_t imageCount() { return swapChainImages.size(); }
  vk::Format getSwapChainImageFormat() { return swapChainImageFormat; }
  vk::Format getSwapChainDepthFormat() { return swapChainDepthFormat; }
  vk::Extent2D getSwapChainExtent() { return swapChainExtent; }
  uint32_t width() { return swapChainExtent.width; }
  uint32_t height() { return swapChainExtent.height; }

  float extentAspectRatio() {
    return static_cast<float>(swapChainExtent.width) /
           static_cast<float>(swapChainExtent.height);
  }
  vk::Format findDepthFormat();

  vk::Result acquireNextImage(uint32_t *imageIndex);
  vk::Result submitCommandBuffers(const vk::CommandBuffer *buffers,
                                  uint32_t *imageIndex);

  bool compareSwapFormats(const SwapChain &swapChain) const {
    return swapChain.swapChainDepthFormat == swapChainDepthFormat &&
           swapChain.swapChainImageFormat == swapChainImageFormat;
  }

  vk::RenderPass renderPass;
  vk::Extent2D swapChainExtent;

private:
  void init();
  void createSwapChain();
  void createImageViews();
  void createColorResources();
  void createDepthResources();
  void createRenderPass();
  void createFramebuffers();
  void createSyncObjects();

  bool isFormatSupported(vk::PhysicalDevice physicalDevice, vk::Format format,
                         vk::ImageTiling tiling, vk::ImageUsageFlags usage);
  vk::SurfaceFormatKHR chooseSwapSurfaceFormat(
      const std::vector<vk::SurfaceFormatKHR> &availableFormats);
  vk::PresentModeKHR chooseSwapPresentMode(
      const std::vector<vk::PresentModeKHR> &availablePresentModes);
  vk::Extent2D chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities);

  vk::Format swapChainImageFormat;
  vk::Format swapChainDepthFormat;

  std::vector<vk::Framebuffer> swapChainFramebuffers;

  std::vector<Image> colorImages;
  std::vector<Image> depthImages;
  std::vector<Image> resolvedDepthImages;

  std::vector<vk::Image> swapChainImages;
  std::vector<vk::ImageView> swapChainImageViews;

  EngineContext &context;

  vk::SwapchainKHR swapChain;
  std::shared_ptr<SwapChain> oldSwapChain;

  std::vector<vk::Semaphore> imageAvailableSemaphores;
  std::vector<vk::Semaphore> renderFinishedSemaphores;
  std::vector<vk::Fence> inFlightFences;
  std::vector<vk::Fence> imagesInFlight;
  size_t currentFrame = 0;
};

} // namespace vkh
