#include "renderer.hpp"

#include "swapChain.hpp"

#include <array>
#include <stdexcept>

namespace vkh {
namespace renderer {
std::vector<VkCommandBuffer> commandBuffers;

void recreateSwapChain(EngineContext &context) {
  auto extent = context.window.getExtent();
  while (extent.width == 0 || extent.height == 0) {
    extent = context.window.getExtent();
    glfwWaitEvents();
  }
  vkDeviceWaitIdle(context.vulkan.device);

  if (context.vulkan.swapChain == nullptr) {
    context.vulkan.swapChain = std::make_unique<SwapChain>(context);
  } else {
    std::shared_ptr<SwapChain> oldSwapChain =
        std::move(context.vulkan.swapChain);
    context.vulkan.swapChain =
        std::make_unique<SwapChain>(context, oldSwapChain);

    if (!oldSwapChain->compareSwapFormats(*context.vulkan.swapChain)) {
      throw std::runtime_error(
          "Swap chain image(or depth) format has changed!");
    }
  }
}

void createCommandBuffers(EngineContext &context) {
  commandBuffers.resize(context.vulkan.maxFramesInFlight);

  VkCommandBufferAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
  allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
  allocInfo.commandPool = context.vulkan.commandPool;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

  if (vkAllocateCommandBuffers(context.vulkan.device, &allocInfo,
                               commandBuffers.data()) != VK_SUCCESS) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void init(EngineContext &context) {
  recreateSwapChain(context);
  createCommandBuffers(context);
}

void freeCommandBuffers(EngineContext &context) {
  vkFreeCommandBuffers(context.vulkan.device, context.vulkan.commandPool,
                       static_cast<uint32_t>(commandBuffers.size()),
                       commandBuffers.data());
  commandBuffers.clear();
}

void cleanup(EngineContext &context) {
  context.vulkan.swapChain = nullptr;
  freeCommandBuffers(context);
}

uint32_t currentImageIndex;
int currentFrameIndex{0};
bool isFrameStarted{false};

bool isFrameInProgress() { return isFrameStarted; }

VkCommandBuffer getCurrentCommandBuffer() {
  return commandBuffers[currentFrameIndex];
}

int getFrameIndex() { return currentFrameIndex; }

VkCommandBuffer beginFrame(EngineContext &context) {
  auto result = context.vulkan.swapChain->acquireNextImage(&currentImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR) {
    recreateSwapChain(context);
    return nullptr;
  }

  if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  isFrameStarted = true;

  auto commandBuffer = getCurrentCommandBuffer();
  VkCommandBufferBeginInfo beginInfo{};
  beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;

  if (vkBeginCommandBuffer(commandBuffer, &beginInfo) != VK_SUCCESS) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }
  return commandBuffer;
}

void endFrame(EngineContext &context) {
  auto commandBuffer = getCurrentCommandBuffer();
  if (vkEndCommandBuffer(commandBuffer) != VK_SUCCESS) {
    throw std::runtime_error("failed to record command buffer!");
  }

  auto result = context.vulkan.swapChain->submitCommandBuffers(
      &commandBuffer, &currentImageIndex);
  if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR ||
      context.window.framebufferResized) {
    context.window.framebufferResized = false;
    recreateSwapChain(context);
  } else if (result != VK_SUCCESS) {
    throw std::runtime_error("failed to present swap chain image!");
  }

  isFrameStarted = false;
  currentFrameIndex = (currentFrameIndex + 1) % context.vulkan.maxFramesInFlight;
}

void beginSwapChainRenderPass(EngineContext &context,
                              VkCommandBuffer commandBuffer) {
  VkRenderPassBeginInfo renderPassInfo{};
  renderPassInfo.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
  renderPassInfo.renderPass = context.vulkan.swapChain->renderPass;
  renderPassInfo.framebuffer =
      context.vulkan.swapChain->getFrameBuffer(currentImageIndex);

  renderPassInfo.renderArea.offset = {0, 0};
  renderPassInfo.renderArea.extent = context.vulkan.swapChain->swapChainExtent;

  std::array<VkClearValue, 2> clearValues{};
  clearValues[0].color = {0.01f, 0.01f, 0.01f, 1.0f};
  clearValues[1].depthStencil = {1.0f, 0};
  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  vkCmdBeginRenderPass(commandBuffer, &renderPassInfo,
                       VK_SUBPASS_CONTENTS_INLINE);

  VkViewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width =
      static_cast<float>(context.vulkan.swapChain->swapChainExtent.width);
  viewport.height =
      static_cast<float>(context.vulkan.swapChain->swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  VkRect2D scissor{{0, 0}, context.vulkan.swapChain->swapChainExtent};
  vkCmdSetViewport(commandBuffer, 0, 1, &viewport);
  vkCmdSetScissor(commandBuffer, 0, 1, &scissor);
}

void endSwapChainRenderPass(VkCommandBuffer commandBuffer) {
  vkCmdEndRenderPass(commandBuffer);
}
} // namespace renderer
} // namespace vkh
