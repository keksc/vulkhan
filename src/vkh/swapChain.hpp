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

  vk::ImageView getImageView(size_t index) {
    return swapChainImageViews[index];
  }
  vk::ImageView getStorageImageView(size_t index) {
    return swapChainImageViews[index];
  }
  vk::ImageView getDepthImageView(size_t index) {
    return depthImages[index].getImageView();
  }
  vk::ImageView getResolvedDepthImageView(size_t index) {
    return resolvedDepthImages[index].getView();
  }
  vk::ImageView getMsaaColorView(size_t index) {
    return colorImages[index].getView();
  }
  vk::ImageView getMsaaDepthView(size_t index) {
    return depthImages[index].getView();
  }
  vk::ImageView getSceneColorView(size_t index) {
    return sceneColorImages[index].getView();
  }

  vk::Image getSceneColorImage(size_t index) { return sceneColorImages[index]; }
  vk::Image getMsaaColorImage(size_t index) { return colorImages[index]; }
  vk::Image getImage(size_t index) const { return swapChainImages[index]; }
  vk::Image getDepthImage(size_t index) { return depthImages[index]; }
  vk::Image getResolvedDepthImage(size_t index) {
    return resolvedDepthImages[index];
  }
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

  vk::Extent2D swapChainExtent;

private:
  void init();
  void createSwapChain();
  void createImageViews();
  void createColorResources();
  void createDepthResources();
  void createSceneColorResources();
  void transitionNewAttachmentImages();
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
  std::vector<Image2D> colorImages;
  std::vector<Image2D> depthImages;
  std::vector<Image2D> resolvedDepthImages;
  std::vector<Image2D> sceneColorImages;
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
