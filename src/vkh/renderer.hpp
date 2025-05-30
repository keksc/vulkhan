#pragma once

#include <vulkan/vulkan_core.h>

#include "engineContext.hpp"

namespace vkh {
namespace renderer {
void init(EngineContext &context);
void cleanup(EngineContext &context);

bool isFrameInProgress();

VkCommandBuffer getCurrentCommandBuffer();

int getFrameIndex();

VkCommandBuffer beginFrame(EngineContext &context);
void endFrame(EngineContext &context);
void beginSwapChainRenderPass(EngineContext &context,
                              VkCommandBuffer commandBuffer);
void endSwapChainRenderPass(VkCommandBuffer commandBuffer);
} // namespace renderer
} // namespace vkh
