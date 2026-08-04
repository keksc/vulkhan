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
uint32_t getCurrentImageIndex() { return currentImageIndex; }

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
  // No final layout-transition barrier needed here: endHudPass() already
  // leaves the swapchain image in ePresentSrcKHR.
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

static void setViewportScissor(vk::CommandBuffer commandBuffer,
                               vk::Extent2D extent) {
  vk::Viewport viewport{};
  viewport.x = 0.0f;
  viewport.y = 0.0f;
  viewport.width = static_cast<float>(extent.width);
  viewport.height = static_cast<float>(extent.height);
  viewport.minDepth = 0.0f;
  viewport.maxDepth = 1.0f;
  vk::Rect2D scissor{vk::Offset2D{0, 0}, extent};
  commandBuffer.setViewport(0, 1, &viewport);
  commandBuffer.setScissor(0, 1, &scissor);
}

void beginMsaaPass(EngineContext &context, vk::CommandBuffer commandBuffer) {
  auto &swapChain = *context.vulkan.swapChain;
  auto extent = swapChain.getSwapChainExtent();
  std::array<vk::ImageMemoryBarrier2, 4> resetBarriers{};

  // MSAA color attachment: this image is transitioned once at creation time
  // (transitionNewAttachmentImages) and, unlike the resolve targets below,
  // is never sampled or otherwise read elsewhere in the frame, so it never
  // actually leaves eColorAttachmentOptimal in practice. We still barrier it
  // explicitly here so beginRendering()'s declared layout is backed by a
  // real transition instead of depending on that invariant staying true.
  auto &toMsaaColor = resetBarriers[0];
  toMsaaColor.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  toMsaaColor.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  toMsaaColor.dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  toMsaaColor.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  toMsaaColor.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
  toMsaaColor.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
  toMsaaColor.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toMsaaColor.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toMsaaColor.image = swapChain.getMsaaColorImage(currentImageIndex);
  toMsaaColor.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

  // MSAA depth attachment: same reasoning as above.
  auto &toMsaaDepth = resetBarriers[1];
  toMsaaDepth.srcStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                             vk::PipelineStageFlagBits2::eLateFragmentTests;
  toMsaaDepth.srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  toMsaaDepth.dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests |
                             vk::PipelineStageFlagBits2::eLateFragmentTests;
  toMsaaDepth.dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  toMsaaDepth.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  toMsaaDepth.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  toMsaaDepth.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toMsaaDepth.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toMsaaDepth.image = swapChain.getDepthImage(currentImageIndex);
  toMsaaDepth.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};

  auto &toColorAttachment = resetBarriers[2];
  toColorAttachment.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
  toColorAttachment.srcAccessMask = vk::AccessFlagBits2::eNone;
  toColorAttachment.dstStageMask =
      vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  toColorAttachment.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  toColorAttachment.oldLayout = vk::ImageLayout::eUndefined;
  toColorAttachment.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
  toColorAttachment.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toColorAttachment.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toColorAttachment.image = swapChain.getSceneColorImage(currentImageIndex);
  toColorAttachment.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1,
                                        0, 1};

  // Resolved depth: postprocessing left this in eDepthStencilReadOnlyOptimal
  // last frame (it's sampled there as an input). Reset it back to
  // eDepthStencilAttachmentOptimal so it can be written as this frame's
  // resolve target. Contents don't matter going in (loadOp is eClear below),
  // so eUndefined as the barrier's declared old layout is valid and avoids
  // having to track the exact previous state.
  auto &toDepthAttachment = resetBarriers[3];
  toDepthAttachment.srcStageMask = vk::PipelineStageFlagBits2::eTopOfPipe;
  toDepthAttachment.srcAccessMask = vk::AccessFlagBits2::eNone;
  toDepthAttachment.dstStageMask =
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
      vk::PipelineStageFlagBits2::eLateFragmentTests;
  toDepthAttachment.dstAccessMask =
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  toDepthAttachment.oldLayout = vk::ImageLayout::eUndefined;
  toDepthAttachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  toDepthAttachment.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toDepthAttachment.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toDepthAttachment.image = swapChain.getResolvedDepthImage(currentImageIndex);
  toDepthAttachment.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1,
                                        0, 1};

  vk::DependencyInfo depInfo{};
  depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(resetBarriers.size());
  depInfo.pImageMemoryBarriers = resetBarriers.data();
  commandBuffer.pipelineBarrier2(&depInfo);

  vk::RenderingAttachmentInfo colorAttachment{};
  colorAttachment.imageView = swapChain.getMsaaColorView(currentImageIndex);
  colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  colorAttachment.resolveMode = vk::ResolveModeFlagBits::eAverage;
  colorAttachment.resolveImageView =
      swapChain.getSceneColorView(currentImageIndex);
  colorAttachment.resolveImageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  colorAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  colorAttachment.clearValue.color =
      vk::ClearColorValue{std::array<float, 4>{0.0f, 0.0f, 0.0f, 1.0f}};

  vk::RenderingAttachmentInfo depthAttachment{};
  depthAttachment.imageView = swapChain.getMsaaDepthView(currentImageIndex);
  depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depthAttachment.resolveMode = vk::ResolveModeFlagBits::eSampleZero;
  depthAttachment.resolveImageView =
      swapChain.getResolvedDepthImageView(currentImageIndex);
  depthAttachment.resolveImageLayout =
      vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depthAttachment.loadOp = vk::AttachmentLoadOp::eClear;
  depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;
  depthAttachment.clearValue.depthStencil = vk::ClearDepthStencilValue{0.0f, 0};

  vk::RenderingInfo renderInfo{};
  renderInfo.renderArea = vk::Rect2D{vk::Offset2D{0, 0}, extent};
  renderInfo.layerCount = 1;
  renderInfo.colorAttachmentCount = 1;
  renderInfo.pColorAttachments = &colorAttachment;
  renderInfo.pDepthAttachment = &depthAttachment;
  commandBuffer.beginRendering(&renderInfo);
  setViewportScissor(commandBuffer, extent);
}

void transitionToOneXPass(EngineContext &context,
                          vk::CommandBuffer commandBuffer) {
  commandBuffer.endRendering();
  auto &swapChain = *context.vulkan.swapChain;
  std::array<vk::ImageMemoryBarrier2, 2> barriers{};
  barriers[0].srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  barriers[0].srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  barriers[0].dstStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  barriers[0].dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  barriers[0].oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
  barriers[0].newLayout = vk::ImageLayout::eColorAttachmentOptimal;
  barriers[0].srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  barriers[0].dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  barriers[0].image = swapChain.getSceneColorImage(currentImageIndex);
  barriers[0].subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

  barriers[1].srcStageMask = vk::PipelineStageFlagBits2::eLateFragmentTests |
                             vk::PipelineStageFlagBits2::eEarlyFragmentTests;
  barriers[1].srcAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  barriers[1].dstStageMask = vk::PipelineStageFlagBits2::eEarlyFragmentTests;
  barriers[1].dstAccessMask = vk::AccessFlagBits2::eDepthStencilAttachmentRead;
  barriers[1].oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  barriers[1].newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  barriers[1].srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  barriers[1].dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  barriers[1].image = swapChain.getResolvedDepthImage(currentImageIndex);
  barriers[1].subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};

  vk::DependencyInfo depInfo{};
  depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
  depInfo.pImageMemoryBarriers = barriers.data();
  commandBuffer.pipelineBarrier2(&depInfo);

  auto extent = swapChain.getSwapChainExtent();
  vk::RenderingAttachmentInfo colorAttachment{};
  colorAttachment.imageView = swapChain.getSceneColorView(currentImageIndex);
  colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  colorAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
  colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

  vk::RenderingAttachmentInfo depthAttachment{};
  depthAttachment.imageView =
      swapChain.getResolvedDepthImageView(currentImageIndex);
  depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depthAttachment.loadOp = vk::AttachmentLoadOp::eLoad;
  depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;

  vk::RenderingInfo renderInfo{};
  renderInfo.renderArea = vk::Rect2D{vk::Offset2D{0, 0}, extent};
  renderInfo.layerCount = 1;
  renderInfo.colorAttachmentCount = 1;
  renderInfo.pColorAttachments = &colorAttachment;
  renderInfo.pDepthAttachment = &depthAttachment;
  commandBuffer.beginRendering(&renderInfo);
  setViewportScissor(commandBuffer, extent);
}

void endOneXPass(vk::CommandBuffer commandBuffer) {
  commandBuffer.endRendering();
}

void beginHudPass(EngineContext &context, vk::CommandBuffer commandBuffer) {
  auto &swapChain = *context.vulkan.swapChain;
  auto extent = swapChain.getSwapChainExtent();

  // PostProcessingSys::run() (both the compute path and runPassthrough())
  // leaves the swapchain image in ePresentSrcKHR. Bring it back to
  // eColorAttachmentOptimal so the HUD can draw directly onto it.
  vk::ImageMemoryBarrier2 toColorAttachment{};
  toColorAttachment.srcStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
  toColorAttachment.srcAccessMask = vk::AccessFlagBits2::eNone;
  toColorAttachment.dstStageMask =
      vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  toColorAttachment.dstAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  toColorAttachment.oldLayout = vk::ImageLayout::ePresentSrcKHR;
  toColorAttachment.newLayout = vk::ImageLayout::eColorAttachmentOptimal;
  toColorAttachment.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toColorAttachment.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toColorAttachment.image = swapChain.getImage(currentImageIndex);
  toColorAttachment.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1,
                                        0, 1};

  // HudSys has depthTestEnable, so its pipeline was created with a
  // depthAttachmentFormat and this pass must supply a real depth
  // attachment - a null pDepthAttachment would mismatch what the bound
  // pipeline expects. PostProcessingSys::run() left the resolved depth in
  // eDepthStencilReadOnlyOptimal (it sampled it as input); bring it back
  // to an attachment layout so the HUD can test/write against it.
  vk::ImageMemoryBarrier2 toDepthAttachment{};
  toDepthAttachment.srcStageMask = vk::PipelineStageFlagBits2::eComputeShader;
  toDepthAttachment.srcAccessMask = vk::AccessFlagBits2::eShaderRead;
  toDepthAttachment.dstStageMask =
      vk::PipelineStageFlagBits2::eEarlyFragmentTests |
      vk::PipelineStageFlagBits2::eLateFragmentTests;
  toDepthAttachment.dstAccessMask =
      vk::AccessFlagBits2::eDepthStencilAttachmentRead |
      vk::AccessFlagBits2::eDepthStencilAttachmentWrite;
  toDepthAttachment.oldLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
  toDepthAttachment.newLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  toDepthAttachment.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toDepthAttachment.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toDepthAttachment.image = swapChain.getResolvedDepthImage(currentImageIndex);
  toDepthAttachment.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1,
                                        0, 1};

  std::array<vk::ImageMemoryBarrier2, 2> barriers{toColorAttachment,
                                                  toDepthAttachment};
  vk::DependencyInfo depInfo{};
  depInfo.imageMemoryBarrierCount = static_cast<uint32_t>(barriers.size());
  depInfo.pImageMemoryBarriers = barriers.data();
  commandBuffer.pipelineBarrier2(&depInfo);

  vk::RenderingAttachmentInfo colorAttachment{};
  colorAttachment.imageView = swapChain.getImageView(currentImageIndex);
  colorAttachment.imageLayout = vk::ImageLayout::eColorAttachmentOptimal;
  colorAttachment.loadOp =
      vk::AttachmentLoadOp::eLoad; // keep postprocessing's output
  colorAttachment.storeOp = vk::AttachmentStoreOp::eStore;

  vk::RenderingAttachmentInfo depthAttachment{};
  depthAttachment.imageView =
      swapChain.getResolvedDepthImageView(currentImageIndex);
  depthAttachment.imageLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depthAttachment.loadOp =
      vk::AttachmentLoadOp::eLoad; // preserve world depth for occlusion
  depthAttachment.storeOp = vk::AttachmentStoreOp::eDontCare;

  vk::RenderingInfo renderInfo{};
  renderInfo.renderArea = vk::Rect2D{vk::Offset2D{0, 0}, extent};
  renderInfo.layerCount = 1;
  renderInfo.colorAttachmentCount = 1;
  renderInfo.pColorAttachments = &colorAttachment;
  renderInfo.pDepthAttachment = &depthAttachment;

  commandBuffer.beginRendering(&renderInfo);
  setViewportScissor(commandBuffer, extent);
}

void endHudPass(EngineContext &context, vk::CommandBuffer commandBuffer) {
  commandBuffer.endRendering();

  // Back to present layout for the actual present call.
  vk::ImageMemoryBarrier2 toPresent{};
  toPresent.srcStageMask = vk::PipelineStageFlagBits2::eColorAttachmentOutput;
  toPresent.srcAccessMask = vk::AccessFlagBits2::eColorAttachmentWrite;
  toPresent.dstStageMask = vk::PipelineStageFlagBits2::eBottomOfPipe;
  toPresent.dstAccessMask = vk::AccessFlagBits2::eNone;
  toPresent.oldLayout = vk::ImageLayout::eColorAttachmentOptimal;
  toPresent.newLayout = vk::ImageLayout::ePresentSrcKHR;
  toPresent.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  toPresent.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  toPresent.image = context.vulkan.swapChain->getImage(currentImageIndex);
  toPresent.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

  vk::DependencyInfo depInfo{};
  depInfo.imageMemoryBarrierCount = 1;
  depInfo.pImageMemoryBarriers = &toPresent;
  commandBuffer.pipelineBarrier2(&depInfo);
}

} // namespace renderer
} // namespace vkh
