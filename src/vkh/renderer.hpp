#pragma once

#include "swapChain.hpp"

// std
#include <cassert>
#include <memory>
#include <vector>

namespace vkh {
	namespace renderer {
		void init(EngineContext& context);
		void cleanup(EngineContext& context);

		VkRenderPass getSwapChainRenderPass(EngineContext& context);
		bool isFrameInProgress();

		VkCommandBuffer getCurrentCommandBuffer();

		int getFrameIndex();

		VkCommandBuffer beginFrame(EngineContext& context);
		void endFrame(EngineContext& context);
		void beginSwapChainRenderPass(EngineContext& context, VkCommandBuffer commandBuffer);
		void endSwapChainRenderPass(VkCommandBuffer commandBuffer);
	}
}
