#pragma once

#include "../engineContext.hpp"
#include "../camera.hpp"
#include "../frameInfo.hpp"
#include "../gameObject.hpp"
#include "../pipeline.hpp"

// std
#include <memory>
#include <vector>

namespace vkh {
	class SimpleRenderSystem {
	public:
		SimpleRenderSystem(EngineContext& context, VkDescriptorSetLayout globalSetLayout);
		~SimpleRenderSystem();

		SimpleRenderSystem(const SimpleRenderSystem&) = delete;
		SimpleRenderSystem& operator=(const SimpleRenderSystem&) = delete;

		void renderGameObjects(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
		void createPipeline();

		EngineContext& context;

		std::unique_ptr<LvePipeline> lvePipeline;
		VkPipelineLayout pipelineLayout;
	};
}  // namespace lve
