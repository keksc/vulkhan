#include "swapChain.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

#include "deviceHelpers.hpp"

namespace vkh {

SwapChain::SwapChain(EngineContext &context) : context{context} { init(); }

SwapChain::SwapChain(EngineContext &context,
                     std::shared_ptr<SwapChain> previous)
    : context{context}, oldSwapChain{previous} {
  init();
}

void SwapChain::init() {
  createSwapChain();
  createImageViews();
  createRenderPass();
  createColorResources();
  createDepthResources();
  createFramebuffers();
  createSyncObjects();
}

SwapChain::~SwapChain() {
  auto device = context.vulkan.device;

  for (auto framebuffer : swapChainFramebuffers) {
    device.destroyFramebuffer(framebuffer, nullptr);
  }
  swapChainFramebuffers.clear();

  colorImages.clear();
  depthImages.clear();
  resolvedDepthImages.clear();

  for (auto imageView : swapChainImageViews) {
    device.destroyImageView(imageView, nullptr);
  }
  swapChainImageViews.clear();

  if (swapChain) {
    device.destroySwapchainKHR(swapChain, nullptr);
    swapChain = nullptr;
  }

  device.destroyRenderPass(renderPass, nullptr);

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
  createInfo.imageUsage = vk::ImageUsageFlagBits::eColorAttachment;

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

void SwapChain::createRenderPass() {
  vk::SampleCountFlagBits msaa = context.vulkan.msaaSamples;

  // ATTACHMENT 0: 1x Swapchain Color (Final Output)
  vk::AttachmentDescription2 colorResolveAttachment{};
  colorResolveAttachment.format = getSwapChainImageFormat();
  colorResolveAttachment.samples = vk::SampleCountFlagBits::e1;
  colorResolveAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
  colorResolveAttachment.storeOp = vk::AttachmentStoreOp::eStore;
  colorResolveAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  colorResolveAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  colorResolveAttachment.initialLayout = vk::ImageLayout::eUndefined;
  colorResolveAttachment.finalLayout = vk::ImageLayout::ePresentSrcKHR;

  // ATTACHMENT 1: 1x Depth (Resolved from MSAA)
  vk::AttachmentDescription2 depthResolveAttachment{};
  depthResolveAttachment.format = getSwapChainDepthFormat();
  depthResolveAttachment.samples = vk::SampleCountFlagBits::e1;
  depthResolveAttachment.loadOp = vk::AttachmentLoadOp::eDontCare;
  depthResolveAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  depthResolveAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  depthResolveAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  depthResolveAttachment.initialLayout = vk::ImageLayout::eUndefined;
  depthResolveAttachment.finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // ATTACHMENT 2: MSAA Color
  vk::AttachmentDescription2 msaaColorAttachment{};
  msaaColorAttachment.format = getSwapChainImageFormat();
  msaaColorAttachment.samples = msaa;
  msaaColorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  msaaColorAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  msaaColorAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  msaaColorAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  msaaColorAttachment.initialLayout = vk::ImageLayout::eUndefined;
  msaaColorAttachment.finalLayout = vk::ImageLayout::eColorAttachmentOptimal;

  // ATTACHMENT 3: MSAA Depth
  vk::AttachmentDescription2 msaaDepthAttachment{};
  msaaDepthAttachment.format = getSwapChainDepthFormat();
  msaaDepthAttachment.samples = msaa;
  msaaDepthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  msaaDepthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  msaaDepthAttachment.stencilLoadOp = vk::AttachmentLoadOp::eDontCare;
  msaaDepthAttachment.stencilStoreOp = vk::AttachmentStoreOp::eDontCare;
  msaaDepthAttachment.initialLayout = vk::ImageLayout::eUndefined;
  msaaDepthAttachment.finalLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // REFERENCES
  vk::AttachmentReference2 colorResolveRef{};
  colorResolveRef.attachment = 0;
  colorResolveRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::AttachmentReference2 depthResolveRef{};
  depthResolveRef.attachment = 1;
  depthResolveRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  vk::AttachmentReference2 msaaColorRef{};
  msaaColorRef.attachment = 2;
  msaaColorRef.layout = vk::ImageLayout::eColorAttachmentOptimal;

  vk::AttachmentReference2 msaaDepthRef{};
  msaaDepthRef.attachment = 3;
  msaaDepthRef.layout = vk::ImageLayout::eDepthStencilAttachmentOptimal;

  // SUBPASS 0: MSAA Pass
  vk::SubpassDescriptionDepthStencilResolve depthResolveStruct{};
  depthResolveStruct.depthResolveMode = vk::ResolveModeFlagBits::eSampleZero;
  depthResolveStruct.stencilResolveMode = vk::ResolveModeFlagBits::eNone;
  depthResolveStruct.pDepthStencilResolveAttachment = &depthResolveRef;

  vk::SubpassDescription2 subpass0{};
  subpass0.pNext = &depthResolveStruct;
  subpass0.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass0.colorAttachmentCount = 1;
  subpass0.pColorAttachments = &msaaColorRef;
  subpass0.pDepthStencilAttachment = &msaaDepthRef;
  subpass0.pResolveAttachments = &colorResolveRef;

  // SUBPASS 1: 1x Pass
  vk::SubpassDescription2 subpass1{};
  subpass1.pipelineBindPoint = vk::PipelineBindPoint::eGraphics;
  subpass1.colorAttachmentCount = 1;
  subpass1.pColorAttachments = &colorResolveRef;
  subpass1.pDepthStencilAttachment = &depthResolveRef;

  // DEPENDENCIES
  std::array<vk::SubpassDependency2, 2> dependencies{};

  dependencies[0].srcSubpass = vk::SubpassExternal;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
      vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependencies[0].dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
      vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependencies[0].srcAccessMask = vk::AccessFlagBits::eNone;
  dependencies[0].dstAccessMask =
      vk::AccessFlagBits::eColorAttachmentWrite |
      vk::AccessFlagBits::eDepthStencilAttachmentWrite;

  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = 1;
  dependencies[1].srcStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
      vk::PipelineStageFlagBits::eLateFragmentTests |
      vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependencies[1].dstStageMask =
      vk::PipelineStageFlagBits::eColorAttachmentOutput |
      vk::PipelineStageFlagBits::eEarlyFragmentTests;
  dependencies[1].srcAccessMask =
      vk::AccessFlagBits::eColorAttachmentWrite |
      vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  dependencies[1].dstAccessMask =
      vk::AccessFlagBits::eColorAttachmentWrite |
      vk::AccessFlagBits::eDepthStencilAttachmentRead;

  // CREATE RENDER PASS
  std::array<vk::AttachmentDescription2, 4> attachments = {
      colorResolveAttachment, depthResolveAttachment, msaaColorAttachment,
      msaaDepthAttachment};
  std::array<vk::SubpassDescription2, 2> subpasses = {subpass0, subpass1};

  vk::RenderPassCreateInfo2 renderPassInfo{};
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
  renderPassInfo.pSubpasses = subpasses.data();
  renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  if (context.vulkan.device.createRenderPass2(
          &renderPassInfo, nullptr, &renderPass) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to create multi-subpass render pass!");
  }
}

void SwapChain::createFramebuffers() {
  swapChainFramebuffers.resize(imageCount());

  for (size_t i = 0; i < imageCount(); i++) {
    std::array<vk::ImageView, 4> attachments = {
        swapChainImageViews[i], resolvedDepthImages[i].getView(),
        colorImages[i].getView(), depthImages[i].getView()};

    vk::FramebufferCreateInfo framebufferInfo{};
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (context.vulkan.device.createFramebuffer(&framebufferInfo, nullptr,
                                                &swapChainFramebuffers[i]) !=
        vk::Result::eSuccess) {
      throw std::runtime_error("failed to create framebuffer!");
    }
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
    resolveDepthInfo.usage = vk::ImageUsageFlagBits::eDepthStencilAttachment;
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
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == vk::Format::eB8G8R8A8Srgb &&
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
