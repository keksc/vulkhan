#pragma once

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
void beginMsaaPass(EngineContext &context, vk::CommandBuffer commandBuffer);
void transitionToOneXPass(EngineContext &context,
                          vk::CommandBuffer commandBuffer);
// Ends pass 1. Replaces endSwapChainRenderPass.
void endOneXPass(vk::CommandBuffer commandBuffer);
void beginHudPass(EngineContext &context, vk::CommandBuffer commandBuffer);
void endHudPass(EngineContext &context, vk::CommandBuffer commandBuffer);
} // namespace renderer
} // namespace vkh
