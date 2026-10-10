#pragma once

#include <array>
#include <vulkan/vulkan.hpp>

#include "engineContext.hpp"

namespace vkh {
namespace renderer {
void init(EngineContext &context);
void cleanup(EngineContext &context);
bool isFrameInProgress();

vk::CommandBuffer getCurrentCommandBuffer();

int getFrameIndex();
uint32_t getCurrentImageIndex();

vk::CommandBuffer beginFrame(EngineContext &context);
void endFrame(EngineContext &context);
void beginMsaaPass(EngineContext &context, vk::CommandBuffer commandBuffer,
                   const std::array<float, 4> &clearColor = {0, 0, 0, 1});
// Separating these allows compute work after the opaque depth resolve.
void endMsaaPass(vk::CommandBuffer commandBuffer);
void beginOneXPass(EngineContext &context, vk::CommandBuffer commandBuffer);
void transitionToOneXPass(EngineContext &context,
                          vk::CommandBuffer commandBuffer);
// Ends pass 1. Replaces endSwapChainRenderPass.
void endOneXPass(vk::CommandBuffer commandBuffer);
void beginHudPass(EngineContext &context, vk::CommandBuffer commandBuffer);
void endHudPass(EngineContext &context, vk::CommandBuffer commandBuffer);
} // namespace renderer
} // namespace vkh
