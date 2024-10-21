#include "swapChain.hpp"

// std
#include "deviceHelpers.hpp"
#include <array>
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
#include <set>
#include <stdexcept>

namespace vkh {

namespace swapChain {
VkSurfaceFormatKHR chooseSwapSurfaceFormat(
    const std::vector<VkSurfaceFormatKHR> &availableFormats) {
  for (const auto &availableFormat : availableFormats) {
    if (availableFormat.format == VK_FORMAT_B8G8R8A8_SRGB &&
        availableFormat.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
      return availableFormat;
    }
  }

  return availableFormats[0];
}
VkPresentModeKHR chooseSwapPresentMode(
    const std::vector<VkPresentModeKHR> &availablePresentModes) {
  /*for (const auto &availablePresentMode : availablePresentModes) {
    if (availablePresentMode == VK_PRESENT_MODE_MAILBOX_KHR) {
      std::cout << "Present mode: Mailbox" << std::endl;
      return availablePresentMode;
    }
  }*/

  // for (const auto &availablePresentMode : availablePresentModes) {
  //   if (availablePresentMode == VK_PRESENT_MODE_IMMEDIATE_KHR) {
  //     std::cout << "Present mode: Immediate" << std::endl;
  //     return availablePresentMode;
  //   }
  // }

  std::cout << "Present mode: V-Sync" << std::endl;
  return VK_PRESENT_MODE_FIFO_KHR;
}
VkExtent2D chooseSwapExtent(EngineContext &context,
                            const VkSurfaceCapabilitiesKHR &capabilities) {
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
void createSwapChain(EngineContext &context) {
  SwapChainSupportDetails swapChainSupport = getSwapChainSupport(context);

  VkSurfaceFormatKHR surfaceFormat =
      chooseSwapSurfaceFormat(swapChainSupport.formats);
  VkPresentModeKHR presentMode =
      chooseSwapPresentMode(swapChainSupport.presentModes);
  VkExtent2D extent = chooseSwapExtent(context, swapChainSupport.capabilities);

  uint32_t imageCount = swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount > 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount) {
    imageCount = swapChainSupport.capabilities.maxImageCount;
  }

  VkSwapchainCreateInfoKHR createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
  createInfo.surface = context.vulkan.surface;

  createInfo.minImageCount = imageCount;
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
    createInfo.queueFamilyIndexCount = 0;     // Optional
    createInfo.pQueueFamilyIndices = nullptr; // Optional
  }

  createInfo.preTransform = swapChainSupport.capabilities.currentTransform;
  createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;

  createInfo.presentMode = presentMode;
  createInfo.clipped = VK_TRUE;

  createInfo.oldSwapchain = VK_NULL_HANDLE;//context.vulkan.oldSwapChain == nullptr
                                //? VK_NULL_HANDLE
                                //: context.vulkan.oldSwapChain;

  if (vkCreateSwapchainKHR(context.vulkan.device, &createInfo, nullptr,
                           &context.vulkan.swapChain) != VK_SUCCESS) {
    throw std::runtime_error("failed to create swap chain!");
  }

  // we only specified a minimum number of images in the swap chain, so the
  // implementation is allowed to create a swap chain with more. That's why
  // we'll first query the final number of images with vkGetSwapchainImagesKHR,
  // then resize the container and finally call it again to retrieve the
  // handles.
  vkGetSwapchainImagesKHR(context.vulkan.device, context.vulkan.swapChain,
                          &imageCount, nullptr);
  context.vulkan.swapChainImages.resize(imageCount);
  vkGetSwapchainImagesKHR(context.vulkan.device, context.vulkan.swapChain,
                          &imageCount, context.vulkan.swapChainImages.data());

  context.vulkan.swapChainImageFormat = surfaceFormat.format;
  context.vulkan.swapChainExtent = extent;

  //context.vulkan.oldSwapChain = context.vulkan.swapChain;
}

void createImageViews(EngineContext &context) {
  context.vulkan.swapChainImageViews.resize(
      context.vulkan.swapChainImages.size());
  for (size_t i = 0; i < context.vulkan.swapChainImages.size(); i++) {
    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = context.vulkan.swapChainImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = context.vulkan.swapChainImageFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context.vulkan.device, &viewInfo, nullptr,
                          &context.vulkan.swapChainImageViews[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create texture image view!");
    }
  }
}
VkFormat findDepthFormat(EngineContext &context) {
  return findSupportedFormat(
      context,
      {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D32_SFLOAT_S8_UINT,
       VK_FORMAT_D24_UNORM_S8_UINT},
      VK_IMAGE_TILING_OPTIMAL, VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT);
}
void createRenderPass(EngineContext &context) {
  VkAttachmentDescription depthAttachment{};
  depthAttachment.format = findDepthFormat(context);
  depthAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  depthAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  depthAttachment.storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  depthAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  depthAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  depthAttachment.finalLayout =
      VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentReference depthAttachmentRef{};
  depthAttachmentRef.attachment = 1;
  depthAttachmentRef.layout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;

  VkAttachmentDescription colorAttachment = {};
  colorAttachment.format = context.vulkan.swapChainImageFormat;
  colorAttachment.samples = VK_SAMPLE_COUNT_1_BIT;
  colorAttachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
  colorAttachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
  colorAttachment.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
  colorAttachment.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
  colorAttachment.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
  colorAttachment.finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

  VkAttachmentReference colorAttachmentRef = {};
  colorAttachmentRef.attachment = 0;
  colorAttachmentRef.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

  VkSubpassDescription subpass = {};
  subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
  subpass.colorAttachmentCount = 1;
  subpass.pColorAttachments = &colorAttachmentRef;
  subpass.pDepthStencilAttachment = &depthAttachmentRef;

  VkSubpassDependency dependency = {};
  dependency.dstSubpass = 0;
  dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT |
                             VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
  dependency.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
  dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
  dependency.srcAccessMask = 0;
  dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
                            VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;

  std::array<VkAttachmentDescription, 2> attachments = {colorAttachment,
                                                        depthAttachment};
  VkRenderPassCreateInfo renderPassInfo = {};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
  renderPassInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
  renderPassInfo.pAttachments = attachments.data();
  renderPassInfo.subpassCount = 1;
  renderPassInfo.pSubpasses = &subpass;
  renderPassInfo.dependencyCount = 1;
  renderPassInfo.pDependencies = &dependency;

  if (vkCreateRenderPass(context.vulkan.device, &renderPassInfo, nullptr,
                         &context.vulkan.renderPass) != VK_SUCCESS) {
    throw std::runtime_error("failed to create render pass!");
  }
}
void createDepthResources(EngineContext &context) {
  VkFormat depthFormat = findDepthFormat(context);
  context.vulkan.swapChainDepthFormat = depthFormat;
  VkExtent2D swapChainExtent = context.vulkan.swapChainExtent;

  context.vulkan.depthImages.resize(context.vulkan.swapChainImages.size());
  context.vulkan.depthImageMemorys.resize(
      context.vulkan.swapChainImages.size());
  context.vulkan.depthImageViews.resize(context.vulkan.swapChainImages.size());

  for (int i = 0; i < context.vulkan.depthImages.size(); i++) {
    VkImageCreateInfo imageInfo{};
    imageInfo.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    imageInfo.imageType = VK_IMAGE_TYPE_2D;
    imageInfo.extent.width = swapChainExtent.width;
    imageInfo.extent.height = swapChainExtent.height;
    imageInfo.extent.depth = 1;
    imageInfo.mipLevels = 1;
    imageInfo.arrayLayers = 1;
    imageInfo.format = depthFormat;
    imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
    imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    imageInfo.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
    imageInfo.samples = VK_SAMPLE_COUNT_1_BIT;
    imageInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    imageInfo.flags = 0;

    createImageWithInfo(context, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                        context.vulkan.depthImages[i],
                        context.vulkan.depthImageMemorys[i]);

    VkImageViewCreateInfo viewInfo{};
    viewInfo.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    viewInfo.image = context.vulkan.depthImages[i];
    viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
    viewInfo.format = depthFormat;
    viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
    viewInfo.subresourceRange.baseMipLevel = 0;
    viewInfo.subresourceRange.levelCount = 1;
    viewInfo.subresourceRange.baseArrayLayer = 0;
    viewInfo.subresourceRange.layerCount = 1;

    if (vkCreateImageView(context.vulkan.device, &viewInfo, nullptr,
                          &context.vulkan.depthImageViews[i]) != VK_SUCCESS) {
      throw std::runtime_error("failed to create texture image view!");
    }
  }
}
void createFramebuffers(EngineContext &context) {
  context.vulkan.swapChainFramebuffers.resize(
      context.vulkan.swapChainImages.size());
  for (size_t i = 0; i < context.vulkan.swapChainImages.size(); i++) {
    std::array<VkImageView, 2> attachments = {
        context.vulkan.swapChainImageViews[i],
        context.vulkan.depthImageViews[i]};

    VkExtent2D swapChainExtent = context.vulkan.swapChainExtent;
    VkFramebufferCreateInfo framebufferInfo = {};
    framebufferInfo.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
    framebufferInfo.renderPass = context.vulkan.renderPass;
    framebufferInfo.attachmentCount = static_cast<uint32_t>(attachments.size());
    framebufferInfo.pAttachments = attachments.data();
    framebufferInfo.width = swapChainExtent.width;
    framebufferInfo.height = swapChainExtent.height;
    framebufferInfo.layers = 1;

    if (vkCreateFramebuffer(context.vulkan.device, &framebufferInfo, nullptr,
                            &context.vulkan.swapChainFramebuffers[i]) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to create framebuffer!");
    }
  }
}
void createSyncObjects(EngineContext &context) {
  context.vulkan.imageAvailableSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  context.vulkan.renderFinishedSemaphores.resize(MAX_FRAMES_IN_FLIGHT);
  context.vulkan.inFlightFences.resize(MAX_FRAMES_IN_FLIGHT);
  context.vulkan.imagesInFlight.resize(context.vulkan.swapChainImages.size(),
                                       VK_NULL_HANDLE);

  VkSemaphoreCreateInfo semaphoreInfo = {};
  semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

  VkFenceCreateInfo fenceInfo = {};
  fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    if (vkCreateSemaphore(context.vulkan.device, &semaphoreInfo, nullptr,
                          &context.vulkan.imageAvailableSemaphores[i]) !=
            VK_SUCCESS ||
        vkCreateSemaphore(context.vulkan.device, &semaphoreInfo, nullptr,
                          &context.vulkan.renderFinishedSemaphores[i]) !=
            VK_SUCCESS ||
        vkCreateFence(context.vulkan.device, &fenceInfo, nullptr,
                      &context.vulkan.inFlightFences[i]) != VK_SUCCESS) {
      throw std::runtime_error(
          "failed to create synchronization objects for a frame!");
    }
  }
}
void create(EngineContext &context) {
  createSwapChain(context);
  createImageViews(context);
  createRenderPass(context);
  createDepthResources(context);
  createFramebuffers(context);
  createSyncObjects(context);
}
size_t currentFrame = 0;
VkResult acquireNextImage(EngineContext &context, uint32_t *imageIndex) {
  vkWaitForFences(context.vulkan.device, 1,
                  &context.vulkan.inFlightFences[currentFrame], VK_TRUE,
                  std::numeric_limits<uint64_t>::max());

  VkResult result = vkAcquireNextImageKHR(
      context.vulkan.device, context.vulkan.swapChain,
      std::numeric_limits<uint64_t>::max(),
      context.vulkan
          .imageAvailableSemaphores[currentFrame], // must be a not signaled
                                                   // semaphore
      VK_NULL_HANDLE, imageIndex);

  return result;
}
VkResult submitCommandBuffers(EngineContext& context, const VkCommandBuffer *buffers,
                                            uint32_t *imageIndex) {
  if (context.vulkan.imagesInFlight[*imageIndex] != VK_NULL_HANDLE) {
    vkWaitForFences(context.vulkan.device, 1, &context.vulkan.imagesInFlight[*imageIndex],
                    VK_TRUE, UINT64_MAX);
  }
  context.vulkan.imagesInFlight[*imageIndex] = context.vulkan.inFlightFences[currentFrame];

  VkSubmitInfo submitInfo = {};
  submitInfo.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;

  VkSemaphore waitSemaphores[] = {context.vulkan.imageAvailableSemaphores[currentFrame]};
  VkPipelineStageFlags waitStages[] = {
      VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT};
  submitInfo.waitSemaphoreCount = 1;
  submitInfo.pWaitSemaphores = waitSemaphores;
  submitInfo.pWaitDstStageMask = waitStages;

  submitInfo.commandBufferCount = 1;
  submitInfo.pCommandBuffers = buffers;

  VkSemaphore signalSemaphores[] = {context.vulkan.renderFinishedSemaphores[currentFrame]};
  submitInfo.signalSemaphoreCount = 1;
  submitInfo.pSignalSemaphores = signalSemaphores;

  vkResetFences(context.vulkan.device, 1, &context.vulkan.inFlightFences[currentFrame]);
  if (vkQueueSubmit(context.vulkan.graphicsQueue, 1, &submitInfo,
                    context.vulkan.inFlightFences[currentFrame]) != VK_SUCCESS) {
    throw std::runtime_error("failed to submit draw command buffer!");
  }

  VkPresentInfoKHR presentInfo = {};
  presentInfo.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;

  presentInfo.waitSemaphoreCount = 1;
  presentInfo.pWaitSemaphores = signalSemaphores;

  VkSwapchainKHR swapChains[] = {context.vulkan.swapChain};
  presentInfo.swapchainCount = 1;
  presentInfo.pSwapchains = swapChains;

  presentInfo.pImageIndices = imageIndex;

  auto result = vkQueuePresentKHR(context.vulkan.presentQueue, &presentInfo);

  currentFrame = (currentFrame + 1) % MAX_FRAMES_IN_FLIGHT;

  return result;
}
void cleanup(EngineContext& context) {
  for (auto imageView : context.vulkan.swapChainImageViews) {
    vkDestroyImageView(context.vulkan.device, imageView, nullptr);
  }
  context.vulkan.swapChainImageViews.clear();

  if (context.vulkan.swapChain != nullptr) {
    vkDestroySwapchainKHR(context.vulkan.device, context.vulkan.swapChain, nullptr);
    context.vulkan.swapChain = nullptr;
  }

  for (int i = 0; i < context.vulkan.depthImages.size(); i++) {
    vkDestroyImageView(context.vulkan.device, context.vulkan.depthImageViews[i], nullptr);
    vkDestroyImage(context.vulkan.device, context.vulkan.depthImages[i], nullptr);
    vkFreeMemory(context.vulkan.device, context.vulkan.depthImageMemorys[i], nullptr);
  }

  for (auto framebuffer : context.vulkan.swapChainFramebuffers) {
    vkDestroyFramebuffer(context.vulkan.device, framebuffer, nullptr);
  }

  vkDestroyRenderPass(context.vulkan.device, context.vulkan.renderPass, nullptr);

  // cleanup synchronization objects
  for (size_t i = 0; i < MAX_FRAMES_IN_FLIGHT; i++) {
    vkDestroySemaphore(context.vulkan.device, context.vulkan.renderFinishedSemaphores[i],
                       nullptr);
    vkDestroySemaphore(context.vulkan.device, context.vulkan.imageAvailableSemaphores[i],
                       nullptr);
    vkDestroyFence(context.vulkan.device, context.vulkan.inFlightFences[i], nullptr);
  }
}
/*LveSwapChain::LveSwapChain(EngineContext &context, VkExtent2D windowExtent)
    : context{context}, windowExtent{windowExtent} {
  init();
}

LveSwapChain::LveSwapChain(EngineContext &context, VkExtent2D windowExtent,
                           std::shared_ptr<LveSwapChain> previous)
    : context{context}, windowExtent{windowExtent}, oldSwapChain{previous} {
  init();
  oldSwapChain = nullptr;
}

void LveSwapChain::init() {
  createSwapChain();
  createImageViews();
  createRenderPass();
  createDepthResources();
  createFramebuffers();
  createSyncObjects();
}



*/

} // namespace swapChain
} // namespace vkh
