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

vk::CommandBuffer beginFrame(EngineContext &context);
void endFrame(EngineContext &context);

void beginSwapChainRenderPass(EngineContext &context,
                              vk::CommandBuffer commandBuffer);
void endSwapChainRenderPass(vk::CommandBuffer commandBuffer);

} // namespace renderer
} // namespace vkh
