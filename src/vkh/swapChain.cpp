#include "swapChain.hpp"
#include "deviceHelpers.hpp"
#include <cstdlib>
#include <cstring>
#include <limits>
#include <stdexcept>

namespace vkh {

SwapChain::SwapChain(EngineContext &context) : context{context} { init(); }

SwapChain::SwapChain(EngineContext &context,
                     std::shared_ptr<SwapChain> previous)
    : context{context}, oldSwapChain{previous} {
  init();
}

void SwapChain::init() {
  createSwapChain();
  oldSwapChain = nullptr;
  createImageViews();
  createColorResources();
  createDepthResources();
  createSceneColorResources();
  transitionNewAttachmentImages();
  createSyncObjects();
}

void SwapChain::createSceneColorResources() {
  vk::Extent2D extent = getSwapChainExtent();
  sceneColorImages.reserve(imageCount());
  for (size_t i = 0; i < imageCount(); i++) {
    ImageCreateInfo_empty info{};
    info.size = {extent.width, extent.height};
    info.format = swapChainImageFormat;
    info.usage = vk::ImageUsageFlagBits::eColorAttachment |
                 vk::ImageUsageFlagBits::eSampled;
    info.layout = vk::ImageLayout::eUndefined;
    info.samples = vk::SampleCountFlagBits::e1;
    info.aspect = vk::ImageAspectFlagBits::eColor;
    info.name = "Scene Color (1x, resolved)";
    sceneColorImages.emplace_back(context, info);
  }
}

void SwapChain::transitionNewAttachmentImages() {
  auto cmd = beginSingleTimeCommands(context);
  for (size_t i = 0; i < imageCount(); i++) {
    // MSAA color: eUndefined -> eColorAttachmentOptimal
    if (!colorImages.empty()) {
      vk::ImageMemoryBarrier2 barrier{};
      barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
      barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
      barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
      barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
      barrier.oldLayout = vk::ImageLayout::eUndefined;
      barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
      barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.image = colorImages[i];
      barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
      vk::DependencyInfo depInfo{};
      depInfo.imageMemoryBarrierCount = 1;
      depInfo.pImageMemoryBarriers = &barrier;
      cmd.pipelineBarrier2(&depInfo);
    }

    // MSAA depth: eUndefined -> eDepthStencilAttachmentOptimal
    {
      vk::ImageMemoryBarrier2 barrier{};
      barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
      barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
      barrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                             vk::PipelineStageFlagBits2::eLateFragmentTests;
      barrier.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
      barrier.oldLayout = vk::ImageLayout::eUndefined;
      barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
      barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.image = depthImages[i];
      barrier.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
      vk::DependencyInfo depInfo{};
      depInfo.imageMemoryBarrierCount = 1;
      depInfo.pImageMemoryBarriers = &barrier;
      cmd.pipelineBarrier2(&depInfo);
    }

    // Resolved depth: eUndefined -> eDepthStencilAttachmentOptimal
    {
      vk::ImageMemoryBarrier2 barrier{};
      barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
      barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
      barrier.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                             vk::PipelineStageFlagBits2::eLateFragmentTests;
      barrier.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
      barrier.oldLayout = vk::ImageLayout::eUndefined;
      barrier.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
      barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.image = resolvedDepthImages[i];
      barrier.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
      vk::DependencyInfo depInfo{};
      depInfo.imageMemoryBarrierCount = 1;
      depInfo.pImageMemoryBarriers = &barrier;
      cmd.pipelineBarrier2(&depInfo);
    }

    // Scene color (1x resolved): eUndefined -> eColorAttachmentOptimal
    {
      vk::ImageMemoryBarrier2 barrier{};
      barrier.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
      barrier.srcAccessMask = vk::AccessFlagBits2::eNone;
      barrier.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
      barrier.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
      barrier.oldLayout = vk::ImageLayout::eUndefined;
      barrier.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
      barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
      barrier.image = sceneColorImages[i];
      barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
      vk::DependencyInfo depInfo{};
      depInfo.imageMemoryBarrierCount = 1;
      depInfo.pImageMemoryBarriers = &barrier;
      cmd.pipelineBarrier2(&depInfo);
    }
  }
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

SwapChain::~SwapChain() {
  auto device = context.vulkan.device;
  colorImages.clear();
  depthImages.clear();
  resolvedDepthImages.clear();
  sceneColorImages.clear();
  for (auto imageView : swapChainImageViews) {
    device.destroyImageView(imageView, nullptr);
  }
  swapChainImageViews.clear();
  if (swapChain) {
    device.destroySwapchainKHR(swapChain, nullptr);
    swapChain = nullptr;
  }
  for (size_t i = 0; i < imageCount(); i++) {
    device.destroySemaphore(renderFinishedSemaphores[i], nullptr);
  }
  for (size_t i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    device.destroySemaphore(imageAvailableSemaphores[i], nullptr);
    device.destroyFence(inFlightFences[i], nullptr);
  }
}

vk::Result SwapChain::acquireNextImage(uint32_t *imageIndex) {
  auto result = context.vulkan.device.waitForFences(
      1, &inFlightFences[currentFrame], vk::True,
      std::numeric_limits<uint64_t>::max());
  if (result != vk::Result::eSuccess)
    return result;
  return context.vulkan.device.acquireNextImageKHR(
      swapChain, std::numeric_limits<uint64_t>::max(),
      imageAvailableSemaphores[currentFrame], nullptr, imageIndex);
}

vk::Result SwapChain::submitCommandBuffers(const vk::CommandBuffer *buffers,
                                           uint32_t *imageIndex) {
  if (imagesInFlight[*imageIndex]) {
    auto result = context.vulkan.device.waitForFences(
        1, &imagesInFlight[*imageIndex], vk::True,
        std::numeric_limits<uint64_t>::max());
    if (result != vk::Result::eSuccess)
      return result;
  }
  imagesInFlight[*imageIndex] = inFlightFences[currentFrame];
  vk::PipelineStageFlags waitStages[] = {
      vk::PipelineStageFlagBits::eColorAttachmentOutput};
  vk::SubmitInfo submitInfo{};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
  submitInfo.pWaitDstStageMask = waitStages;
  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = buffers;
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &renderFinishedSemaphores[*imageIndex];
  if (context.vulkan.device.resetFences(1, &inFlightFences[currentFrame]) !=
      vk::Result::eSuccess)
    throw std::runtime_error("SwapChain error: Failed to reset fences");
  if (context.vulkan.graphicsQueue.submit(
          1, &submitInfo, inFlightFences[currentFrame]) != vk::Result::eSuccess)
    throw std::runtime_error("failed to submit draw command buffer!");
  vk::PresentInfoKHR presentInfo{};
  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinishedSemaphores[*imageIndex];
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapChain;
  presentInfo.pImageIndices = imageIndex;
  currentFrame = (currentFrame + 1) % context.vulkan.maxFramesInFlight;
  return context.vulkan.presentQueue.presentKHR(&presentInfo);
}

void SwapChain::createSwapChain() {
  SwapChainSupportDetails swapChainSupport = getSwapChainSupport(context);
  vk::SurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapChainSupport.formats);
  vk::PresentModeKHR presentMode =
      chooseSwapPresentMode(swapChainSupport.presentModes);
  vk::Extent2D extent = chooseSwapExtent(swapChainSupport.capabilities);
  vk::SwapchainCreateInfoKHR createInfo{};
  createInfo.surface = context.vulkan.surface;
  createInfo.minImageCount = context.vulkan.maxFramesInFlight;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment |
                          vk::ImageUsageFlagBits::eStorage;
  QueueFamilyIndices indices = findPhysicalQueueFamilies(context);
  uint32_t queueFamilyIndices[] = {indices.graphicsFamily,
                                   indices.presentFamily};
  if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = vk::SharingMode::eConcurrent;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = vk::SharingMode::eExclusive;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;
  }
  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = vk::CompositeAlphaFlagBitsKHR::eOpaque;
  createInfo.presentMode = presentMode;
  createInfo.clipped = vk::True;
  createInfo.oldSwapchain =
      oldSwapChain == nullptr ? nullptr : oldSwapChain->swapChain;
  if (context.vulkan.device.createSwapchainKHR(
          &createInfo, nullptr, &swapChain) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to create swap chain!");
  }
  uint32_t actualImageCount;
  if (context.vulkan.device.getSwapchainImagesKHR(
          swapChain, &actualImageCount, nullptr) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to fetch swapchain image count");
  }
  swapChainImages.resize(actualImageCount);
  if (context.vulkan.device.getSwapchainImagesKHR(swapChain, &actualImageCount,
                                                  swapChainImages.data()) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to fetch swapchain images");
  }
  swapChainImageFormat = surfaceFormat.format;
  swapChainExtent = extent;
  swapChainDepthFormat =
      findSupportedFormat(context,
                          {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
                           vk::Format::eD24UnormS8Uint},
                          vk::ImageTiling::eOptimal,
                          vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

void SwapChain::createImageViews() {
  swapChainImageViews.resize(swapChainImages.size());
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    swapChainImageViews[i] =
        createImageView(context, swapChainImages[i], swapChainImageFormat);
  }
}

void SwapChain::createColorResources() {
  if (context.vulkan.msaaSamples == vk::SampleCountFlagBits::e1)
    return;
  vk::Format colorFormat = swapChainImageFormat;
  vk::Extent2D swapChainExtent = getSwapChainExtent();
  colorImages.reserve(imageCount());
  for (size_t i = 0; i < imageCount(); i++) {
    ImageCreateInfo_empty info{};
    info.size = {swapChainExtent.width, swapChainExtent.height};
    info.format = colorFormat;
    info.usage = vk::ImageUsageFlagBits::eTransientAttachment |
                 vk::ImageUsageFlagBits::eColorAttachment;
    info.layout = vk::ImageLayout::eUndefined;
    info.samples = context.vulkan.msaaSamples;
    info.aspect = vk::ImageAspectFlagBits::eColor;
    info.name = "MSAA Color Target";
    colorImages.emplace_back(context, info);
  }
}

void SwapChain::createDepthResources() {
  vk::Format depthFormat = getSwapChainDepthFormat();
  vk::Extent2D swapChainExtent = getSwapChainExtent();
  uint32_t count = imageCount();
  depthImages.clear();
  resolvedDepthImages.clear();
  for (size_t i = 0; i < count; i++) {
    // MSAA Depth Image
    ImageCreateInfo_empty msaaDepthInfo{};
    msaaDepthInfo.format = depthFormat;
    msaaDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                          vk::ImageUsageFlagBits::eTransientAttachment;
    msaaDepthInfo.samples = context.vulkan.msaaSamples;
    msaaDepthInfo.aspect = vk::ImageAspectFlagBits::eDepth;
    msaaDepthInfo.size = {swapChainExtent.width, swapChainExtent.height};
    msaaDepthInfo.name = "MSAA Depth";
    msaaDepthInfo.layout = vk::ImageLayout::eUndefined;
    depthImages.emplace_back(context, msaaDepthInfo);

    // Resolved Depth Image (1x)
    ImageCreateInfo_empty resolveDepthInfo{};
    resolveDepthInfo.format = depthFormat;
    resolveDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment |
                             vk::ImageUsageFlagBits::eSampled;
    resolveDepthInfo.samples = vk::SampleCountFlagBits::e1;
    resolveDepthInfo.aspect = vk::ImageAspectFlagBits::eDepth;
    resolveDepthInfo.size = {swapChainExtent.width, swapChainExtent.height};
    resolveDepthInfo.name = "Resolved Depth (1x)";
    resolveDepthInfo.layout = vk::ImageLayout::eUndefined;
    resolvedDepthImages.emplace_back(context, resolveDepthInfo);
  }
}

void SwapChain::createSyncObjects() {
  imageAvailableSemaphores.resize(context.vulkan.maxFramesInFlight);
  inFlightFences.resize(context.vulkan.maxFramesInFlight);
  imagesInFlight.resize(imageCount(), nullptr);
  renderFinishedSemaphores.resize(imageCount());
  vk::SemaphoreCreateInfo semaphoreInfo{};
  vk::FenceCreateInfo fenceInfo{};
  fenceInfo.flags = vk::FenceCreateFlagBits::eSignaled;
  for (size_t i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    if (context.vulkan.device.createSemaphore(&semaphoreInfo, nullptr,
                                              &imageAvailableSemaphores[i]) !=
            vk::Result::eSuccess ||
        context.vulkan.device.createFence(
            &fenceInfo, nullptr, &inFlightFences[i]) != vk::Result::eSuccess) {
      throw std::runtime_error("failed to create sync objects!");
    }
  }
  for (size_t i = 0; i < imageCount(); i++) {
    if (context.vulkan.device.createSemaphore(&semaphoreInfo, nullptr,
                                              &renderFinishedSemaphores[i]) !=
        vk::Result::eSuccess) {
      throw std::runtime_error("failed to create render finished semaphores!");
    }
  }
}

vk::SurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
    const std::vector<vk::SurfaceFormatKHR> &availableFormats) {
  // We used to prefer an SRGB format here for automatic gamma encoding on
  // writes. But SRGB formats don't support VK_IMAGE_USAGE_STORAGE_BIT at all
  // on most hardware (confirmed by validation: this isn't something mutable
  // format / image view aliasing can work around, since vkCreateSwapchainKHR
  // validates the base imageFormat + imageUsage combo directly). Since
  // PostProcessingSys already does manual tonemapping/gamma in the compute
  // shader, we don't need automatic sRGB encoding from the format itself —
  // so we use a plain UNORM format that supports storage, and gamma-encode
  // manually in the shader before the final imageStore.
  //
  // R8G8B8A8_UNORM also matches the compute shader's declared `rgba8` image
  // layout component order exactly, so no BGRA swizzle is needed either.
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == vk::Format::eR8G8B8A8Unorm &&
        availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      return availableFormat;
    }
  }
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == vk::Format::eB8G8R8A8Unorm &&
        availableFormat.colorSpace == vk::ColorSpaceKHR::eSrgbNonlinear) {
      return availableFormat;
    }
  }
  return availableFormats[0];
}

vk::PresentModeKHR SwapChain::chooseSwapPresentMode(
    const std::vector<vk::PresentModeKHR> &availablePresentModes) {
  return vk::PresentModeKHR::eFifo;
}

vk::Extent2D
SwapChain::chooseSwapExtent(const vk::SurfaceCapabilitiesKHR &capabilities) {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    vk::Extent2D actualExtent = context.window.getExtent();
    actualExtent.width = std::max(
        capabilities.minImageExtent.width,
        std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(
        capabilities.minImageExtent.height,
        std::min(capabilities.maxImageExtent.height, actualExtent.height));
    return actualExtent;
  }
}

vk::Format SwapChain::findDepthFormat() {
  return findSupportedFormat(
      context,
      {vk::Format::eD32Sfloat, vk::Format::eD32SfloatS8Uint,
       vk::Format::eD24UnormS8Uint},
      vk::ImageTiling::eOptimal,
      vk::FormatFeatureFlagBits::eDepthStencilAttachment);
}

} // namespace vkh
