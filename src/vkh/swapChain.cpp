#include "swapChain.hpp"

#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>
#include <vulkan/vulkan_core.h>

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
  for (auto framebuffer : swapChainFramebuffers) {
    vkDestroyFramebuffer(context.vulkan.device, framebuffer, nullptr);
  }
  swapChainFramebuffers.clear();

  colorImages.clear();
  depthImages.clear();
  resolvedDepthImages.clear();

  for (auto imageView : swapChainImageViews) {
    vkDestroyImageView(context.vulkan.device, imageView, nullptr);
  }
  swapChainImageViews.clear();

  if (swapChain != nullptr) {
    vkDestroySwapchainKHR(context.vulkan.device, swapChain, nullptr);
    swapChain = nullptr;
  }

  vkDestroyRenderPass(context.vulkan.device, renderPass, nullptr);

  for (size_t i = 0; i < imageCount(); i++) {
    vkDestroySemaphore(context.vulkan.device, renderFinishedSemaphores[i],
                       nullptr);
  }
  for (size_t i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    vkDestroySemaphore(context.vulkan.device, imageAvailableSemaphores[i],
                       nullptr);
    vkDestroyFence(context.vulkan.device, inFlightFences[i], nullptr);
  }
}

VkResult SwapChain::acquireNextImage(uint32_t *imageIndex) {
  vkWaitForFences(context.vulkan.device, 1, &inFlightFences[currentFrame],
                  VK_TRUE, std::numeric_limits<uint64_t>::max());

  return vkAcquireNextImageKHR(
      context.vulkan.device, swapChain, std::numeric_limits<uint64_t>::max(),
      imageAvailableSemaphores[currentFrame], VK_NULL_HANDLE, imageIndex);
}

VkResult SwapChain::submitCommandBuffers(const VkCommandBuffer *buffers,
                                         uint32_t *imageIndex) {
  if (imagesInFlight[*imageIndex] != VK_NULL_HANDLE) {
    vkWaitForFences(context.vulkan.device, 1, &imagesInFlight[*imageIndex],
                    VK_TRUE, std::numeric_limits<uint32_t>::max());
  }
  imagesInFlight[*imageIndex] = inFlightFences[currentFrame];

  VkSubmitInfo submitInfo{};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = &imageAvailableSemaphores[currentFrame];
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = buffers;

  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = &renderFinishedSemaphores[*imageIndex];

  vkResetFences(context.vulkan.device, 1, &inFlightFences[currentFrame]);
  if (vkQueueSubmit(context.vulkan.graphicsQueue, 1, &submitInfo,
                    inFlightFences[currentFrame]) != VK_SUCCESS) {
    throw std::runtime_error("failed to submit draw command buffer!");
  }

  VkPresentInfoKHR presentInfo{};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = &renderFinishedSemaphores[*imageIndex];

  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = &swapChain;

  presentInfo.pImageIndices = imageIndex;

  currentFrame = (currentFrame + 1) % context.vulkan.maxFramesInFlight;

  return vkQueuePresentKHR(context.vulkan.presentQueue, &presentInfo);
}

void SwapChain::createSwapChain() {
  SwapChainSupportDetails swapChainSupport = getSwapChainSupport(context);

  VkSurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode =
      chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(swapChainSupport.capabilities);

  VkSwapchainCreateInfoKHR createInfo{};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = context.vulkan.surface;

  createInfo.minImageCount = context.vulkan.maxFramesInFlight;
  createInfo.imageFormat = surfaceFormat.format;
  createInfo.imageColorSpace = surfaceFormat.colorSpace;
  createInfo.imageExtent = extent;
  createInfo.imageArrayLayers = 1;
  createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;

  QueueFamilyIndices indices = findPhysicalQueueFamilies(context);
  uint32_t queueFamilyIndices[] = {indices.graphicsFamily,
                                   indices.presentFamily};

  if (indices.graphicsFamily != indices.presentFamily) {
    createInfo.imageSharingMode = VK_SHARING_MODE_CONCURRENT;
    createInfo.queueFamilyIndexCount = 2;
    createInfo.pQueueFamilyIndices = queueFamilyIndices;
  } else {
    createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
    createInfo.queueFamilyIndexCount = 0;
    createInfo.pQueueFamilyIndices = nullptr;
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  createInfo.oldSwapchain =
      oldSwapChain == nullptr ? VK_NULL_HANDLE : oldSwapChain->swapChain;

  if (vkCreateSwapchainKHR(context.vulkan.device, &createInfo, nullptr,
                           &swapChain) != VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
  }

  uint32_t actualImageCount;
  vkGetSwapchainImagesKHR(context.vulkan.device, swapChain, &actualImageCount,
                          nullptr);
  swapChainImages.resize(actualImageCount);
  vkGetSwapchainImagesKHR(context.vulkan.device, swapChain, &actualImageCount,
                          swapChainImages.data());

  swapChainImageFormat = surfaceFormat.format;
  swapChainExtent = extent;
  swapChainDepthFormat = findSupportedFormat(
      context,
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

void SwapChain::createImageViews() {
  swapChainImageViews.resize(swapChainImages.size());
  for (size_t i = 0; i < swapChainImages.size(); i++) {
    swapChainImageViews[i] =
        createImageView(context, swapChainImages[i], swapChainImageFormat);
  }
}

void SwapChain::createRenderPass() {
  VkSampleCountFlagBits msaa = context.vulkan.msaaSamples;

  // ATTACHMENT 0: 1x Swapchain Color (Final Output)
  VkAttachmentDescription2 colorResolveAttachment{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
  colorResolveAttachment.format = getSwapChainImageFormat();
  colorResolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorResolveAttachment.loadOp =
      VK_ATTACHMENT_LOAD_OP_DONT_CARE; // Clear it to black first
  colorResolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorResolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorResolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorResolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorResolveAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  // ATTACHMENT 1: 1x Depth (Resolved from MSAA)
  VkAttachmentDescription2 depthResolveAttachment{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
  depthResolveAttachment.format = getSwapChainDepthFormat();
  depthResolveAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthResolveAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthResolveAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthResolveAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthResolveAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthResolveAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthResolveAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  // ATTACHMENT 2: MSAA Color
  VkAttachmentDescription2 msaaColorAttachment{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
  msaaColorAttachment.format = getSwapChainImageFormat();
  msaaColorAttachment.samples = msaa;
  msaaColorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  msaaColorAttachment.storeOp =
      VK_ATTACHMENT_STORE_OP_DONT_CARE; // We only care about the resolved image
  msaaColorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  msaaColorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  msaaColorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  msaaColorAttachment.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  // ATTACHMENT 3: MSAA Depth
  VkAttachmentDescription2 msaaDepthAttachment{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_DESCRIPTION_2};
  msaaDepthAttachment.format = getSwapChainDepthFormat();
  msaaDepthAttachment.samples = msaa;
  msaaDepthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  msaaDepthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  msaaDepthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  msaaDepthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  msaaDepthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  msaaDepthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  // REFERENCES
  VkAttachmentReference2 colorResolveRef{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
      .attachment = 0,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference2 depthResolveRef{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
      .attachment = 1,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
  VkAttachmentReference2 msaaColorRef{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
      .attachment = 2,
      .layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
  VkAttachmentReference2 msaaDepthRef{
      .sType = VK_STRUCTURE_TYPE_ATTACHMENT_REFERENCE_2,
      .attachment = 3,
      .layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};

  // SUBPASS 0: MSAA Pass
  VkSubpassDescriptionDepthStencilResolve depthResolveStruct{
      .sType = VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_DEPTH_STENCIL_RESOLVE};
  depthResolveStruct.depthResolveMode =
      VK_RESOLVE_MODE_SAMPLE_ZERO_BIT; // Pick the first sample for depth
  depthResolveStruct.stencilResolveMode = VK_RESOLVE_MODE_NONE;
  depthResolveStruct.pDepthStencilResolveAttachment = &depthResolveRef;

  VkSubpassDescription2 subpass0{.sType =
                                     VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
  subpass0.pNext = &depthResolveStruct; // Link depth resolve
  subpass0.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass0.colorAttachmentCount = 1;
  subpass0.pColorAttachments = &msaaColorRef;
  subpass0.pDepthStencilAttachment = &msaaDepthRef;
  subpass0.pResolveAttachments = &colorResolveRef; // Link color resolve

  // SUBPASS 1: 1x Pass (Draws Skybox, Water, Particles, UI)
  VkSubpassDescription2 subpass1{.sType =
                                     VK_STRUCTURE_TYPE_SUBPASS_DESCRIPTION_2};
  subpass1.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass1.colorAttachmentCount = 1;
  subpass1.pColorAttachments =
      &colorResolveRef; // Draw directly to the 1x color buffer
  subpass1.pDepthStencilAttachment =
      &depthResolveRef; // Use the resolved 1x depth buffer

  // DEPENDENCIES
  std::array<VkSubpassDependency2, 2> dependencies{};
  // Dependency 0: External -> Subpass 0
  dependencies[0].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
  dependencies[0].srcSubpass = VK_SUBPASS_EXTERNAL;
  dependencies[0].dstSubpass = 0;
  dependencies[0].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[0].srcAccessMask = 0;
  dependencies[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;

  // Dependency 1: Subpass 0 -> Subpass 1
  dependencies[1].sType = VK_STRUCTURE_TYPE_SUBPASS_DEPENDENCY_2;
  dependencies[1].srcSubpass = 0;
  dependencies[1].dstSubpass = 1;
  dependencies[1].srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[1].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                                 VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependencies[1].srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependencies[1].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                                  VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;

  // CREATE RENDER PASS
  std::array<VkAttachmentDescription2, 4> attachments = {
      colorResolveAttachment, depthResolveAttachment, msaaColorAttachment,
      msaaDepthAttachment};
  std::array<VkSubpassDescription2, 2> subpasses = {subpass0, subpass1};

  VkRenderPassCreateInfo2 renderPassInfo{
      .sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO_2};
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = static_cast<uint32_t>(subpasses.size());
  renderPassInfo.pSubpasses = subpasses.data();
  renderPassInfo.dependencyCount = static_cast<uint32_t>(dependencies.size());
  renderPassInfo.pDependencies = dependencies.data();

  if (vkCreateRenderPass2(context.vulkan.device, &renderPassInfo, nullptr,
                          &renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create multi-subpass render pass!");
  }
}

void SwapChain::createFramebuffers() {
  swapChainFramebuffers.resize(imageCount());

  for (size_t i = 0; i < imageCount(); i++) {
    // Order must match RenderPass Attachment Descriptions:
    // 0: Resolve Color (Swapchain Image)
    // 1: Resolve Depth (1x Resolved Depth)
    // 2: MSAA Color (4x)
    // 3: MSAA Depth (4x)
    std::array<VkImageView, 4> attachments = {
        swapChainImageViews[i],           // 0
        resolvedDepthImages[i].getView(), // 1
        colorImages[i].getView(),         // 2
        depthImages[i].getView()          // 3
    };

    VkFramebufferCreateInfo framebufferInfo{};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(context.vulkan.device, &framebufferInfo, nullptr,
                            &swapChainFramebuffers[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
  }
}

void SwapChain::createColorResources() {
  if (context.vulkan.msaaSamples == VK_SAMPLE_COUNT_1_BIT)
    return;

  VkFormat colorFormat = swapChainImageFormat;
  VkExtent2D swapChainExtent = getSwapChainExtent();

  colorImages.reserve(imageCount());

  for (size_t i = 0; i < imageCount(); i++) {
    ImageCreateInfo_empty info{};
    info.size = {swapChainExtent.width, swapChainExtent.height};
    info.format = colorFormat;
    info.usage = VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT |
                 VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    info.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    info.samples = context.vulkan.msaaSamples;
    info.aspect = VK_IMAGE_ASPECT_COLOR_BIT;
    info.name = "MSAA Color Target";

    colorImages.emplace_back(context, info);
  }
}

void SwapChain::createDepthResources() {
  VkFormat depthFormat = getSwapChainDepthFormat();
  VkExtent2D swapChainExtent = getSwapChainExtent();
  uint32_t count = imageCount();

  depthImages.clear();
  resolvedDepthImages.clear();

  for (size_t i = 0; i < count; i++) {
    // MSAA Depth Image
    ImageCreateInfo_empty msaaDepthInfo{};
    msaaDepthInfo.format = depthFormat;
    msaaDepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT |
                          VK_IMAGE_USAGE_TRANSIENT_ATTACHMENT_BIT;
    msaaDepthInfo.samples = context.vulkan.msaaSamples;
    msaaDepthInfo.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    msaaDepthInfo.size = {swapChainExtent.width, swapChainExtent.height};
    msaaDepthInfo.name = "MSAA Depth";
    msaaDepthInfo.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    depthImages.emplace_back(context, msaaDepthInfo);

    // Resolved Depth Image (1x)
    ImageCreateInfo_empty resolveDepthInfo{};
    resolveDepthInfo.format = depthFormat;
    resolveDepthInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    resolveDepthInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    resolveDepthInfo.aspect = VK_IMAGE_ASPECT_DEPTH_BIT;
    resolveDepthInfo.size = {swapChainExtent.width, swapChainExtent.height};
    resolveDepthInfo.name = "Resolved Depth (1x)";
    resolveDepthInfo.layout = VK_IMAGE_LAYOUT_UNDEFINED;
    resolvedDepthImages.emplace_back(context, resolveDepthInfo);
  }
}

void SwapChain::createSyncObjects() {
  imageAvailableSemaphores.resize(context.vulkan.maxFramesInFlight);
  inFlightFences.resize(context.vulkan.maxFramesInFlight);
  imagesInFlight.resize(imageCount(), VK_NULL_HANDLE);

  // Size this to the number of swapchain images, not maxFramesInFlight
  renderFinishedSemaphores.resize(imageCount());

  VkSemaphoreCreateInfo semaphoreInfo{
      .sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
  VkFenceCreateInfo fenceInfo{.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO,
                              .flags = VK_FENCE_CREATE_SIGNALED_BIT};

  for (size_t i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    if (vkCreateSemaphore(context.vulkan.device, &semaphoreInfo, nullptr,
                          &imageAvailableSemaphores[i]) != VK_SUCCESS ||
        vkCreateFence(context.vulkan.device, &fenceInfo, nullptr,
                      &inFlightFences[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create sync objects!");
    }
  }

  // Create a render finished semaphore PER swapchain image
  for (size_t i = 0; i < imageCount(); i++) {
    if (vkCreateSemaphore(context.vulkan.device, &semaphoreInfo, nullptr,
                          &renderFinishedSemaphores[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create render finished semaphores!");
    }
  }
}

VkSurfaceFormatKHR SwapChain::chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &availableFormats) {
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }

  return availableFormats[0];
}

VkPresentModeKHR SwapChain::chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &availablePresentModes) {
  return VK_PRESENT_MODE_FIFO_KHR;
}

VkExtent2D
SwapChain::chooseSwapExtent(const VkSurfaceCapabilitiesKHR &capabilities) {
  if (capabilities.currentExtent.width !=
      std::numeric_limits<uint32_t>::max()) {
    return capabilities.currentExtent;
  } else {
    VkExtent2D actualExtent = context.window.getExtent();
    actualExtent.width = std::max(
        capabilities.minImageExtent.width,
        std::min(capabilities.maxImageExtent.width, actualExtent.width));
    actualExtent.height = std::max(
        capabilities.minImageExtent.height,
        std::min(capabilities.maxImageExtent.height, actualExtent.height));

    return actualExtent;
  }
}

VkFormat SwapChain::findDepthFormat() {
  return findSupportedFormat(
      context,
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}

} // namespace vkh
