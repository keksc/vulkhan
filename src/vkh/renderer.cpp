#include "renderer.hpp"

#include "swapChain.hpp"

#include <array>
#include <stdexcept>

namespace vkh {
namespace renderer {

std::vector<vk::CommandBuffer> commandBuffers;

uint32_t currentImageIndex;
int currentFrameIndex{0};
bool isFrameStarted{false};

void recreateSwapChain(EngineContext &context) {
  auto extent = context.window.getExtent();
  while (extent.width == 0 || extent.height == 0) {
    extent = context.window.getExtent();
    glfwWaitEvents();
  }
  context.vulkan.device.waitIdle();

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
  currentFrameIndex = 0;
}

void createCommandBuffers(EngineContext &context) {
  commandBuffers.resize(context.vulkan.maxFramesInFlight);

  vk::CommandBufferAllocateInfo allocInfo{};
  allocInfo.level = vk::CommandBufferLevel::ePrimary;
  allocInfo.commandPool = context.vulkan.commandPool;
  allocInfo.commandBufferCount = static_cast<uint32_t>(commandBuffers.size());

  if (context.vulkan.device.allocateCommandBuffers(
          &allocInfo, commandBuffers.data()) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to allocate command buffers!");
  }
}

void init(EngineContext &context) {
  recreateSwapChain(context);
  createCommandBuffers(context);
}

void freeCommandBuffers(EngineContext &context) {
  context.vulkan.device.freeCommandBuffers(
      context.vulkan.commandPool, static_cast<uint32_t>(commandBuffers.size()),
      commandBuffers.data());
  commandBuffers.clear();
}

void cleanup(EngineContext &context) {
  context.vulkan.swapChain = nullptr;
  freeCommandBuffers(context);
}

bool isFrameInProgress() { return isFrameStarted; }

vk::CommandBuffer getCurrentCommandBuffer() {
  return commandBuffers[currentFrameIndex];
}

int getFrameIndex() { return currentFrameIndex; }

vk::CommandBuffer beginFrame(EngineContext &context) {
  auto result = static_cast<vk::Result>(
      context.vulkan.swapChain->acquireNextImage(&currentImageIndex));
  if (result == vk::Result::eErrorOutOfDateKHR) {
    recreateSwapChain(context);
    return nullptr;
  }

  if (result != vk::Result::eSuccess && result != vk::Result::eSuboptimalKHR) {
    throw std::runtime_error("failed to acquire swap chain image!");
  }

  isFrameStarted = true;

  auto commandBuffer = getCurrentCommandBuffer();
  vk::CommandBufferBeginInfo beginInfo{};

  if (commandBuffer.begin(&beginInfo) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to begin recording command buffer!");
  }
  return commandBuffer;
}

void endFrame(EngineContext &context) {
  auto commandBuffer = getCurrentCommandBuffer();
  commandBuffer.end();

  auto result =
      static_cast<vk::Result>(context.vulkan.swapChain->submitCommandBuffers(
          &commandBuffer, &currentImageIndex));

  if (result == vk::Result::eErrorOutOfDateKHR ||
      result == vk::Result::eSuboptimalKHR ||
      context.window.framebufferResized) {
    context.window.framebufferResized = false;
    recreateSwapChain(context);
  } else if (result != vk::Result::eSuccess) {
    throw std::runtime_error("failed to present swap chain image!");
  }

  isFrameStarted = false;
  currentFrameIndex =
      (currentFrameIndex + 1) % context.vulkan.maxFramesInFlight;
}

void beginSwapChainRenderPass(EngineContext &context,
                              vk::CommandBuffer commandBuffer) {
  vk::RenderPassBeginInfo renderPassInfo{};
  renderPassInfo.renderPass = context.vulkan.swapChain->renderPass;
  renderPassInfo.framebuffer =
      context.vulkan.swapChain->getFrameBuffer(currentImageIndex);
  renderPassInfo.renderArea.offset = vk::Offset2D{0, 0};
  renderPassInfo.renderArea.extent = context.vulkan.swapChain->swapChainExtent;

  std::array<vk::ClearValue, 4> clearValues{};
  clearValues[0].color =
      vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
  clearValues[1].depthStencil = vk::ClearDepthStencilValue{0.0f, 0};
  clearValues[2].color =
      vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};
  clearValues[3].depthStencil = vk::ClearDepthStencilValue{0.0f, 0};

  renderPassInfo.clearValueCount = static_cast<uint32_t>(clearValues.size());
  renderPassInfo.pClearValues = clearValues.data();

  commandBuffer.beginRenderPass(&renderPassInfo, vk::SubpassContents::eInline);

  vk::Viewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width =
      static_cast<float>(context.vulkan.swapChain->swapChainExtent.width);
  viewport.height =
      static_cast<float>(context.vulkan.swapChain->swapChainExtent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;

  vk::Rect2D scissor{vk::Offset2D{0, 0},
                     context.vulkan.swapChain->swapChainExtent};

  commandBuffer.setViewport(0, 1, &viewport);
  commandBuffer.setScissor(0, 1, &scissor);
}

void endSwapChainRenderPass(vk::CommandBuffer commandBuffer) {
  commandBuffer.endRenderPass();
}

} // namespace renderer
} // namespace vkh
