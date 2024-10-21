#pragma once

#include "../engineContext.hpp"
#include "../camera.hpp"
#include "../frameInfo.hpp"
#include "../gameObject.hpp"
#include "../pipeline.hpp"

#include <memory>
#include <vector>

namespace vkh {
	class PointLightSystem {
	public:
		PointLightSystem(EngineContext& context, VkDescriptorSetLayout globalSetLayout);
		~PointLightSystem();

		PointLightSystem(const PointLightSystem&) = delete;
		PointLightSystem& operator=(const PointLightSystem&) = delete;

		void update(FrameInfo& frameInfo, GlobalUbo& ubo);
		void render(FrameInfo& frameInfo);

	private:
		void createPipelineLayout(VkDescriptorSetLayout globalSetLayout);
		void createPipeline();

		EngineContext& context;

		std::unique_ptr<LvePipeline> lvePipeline;
		VkPipelineLayout pipelineLayout;
	};
}  // namespace lve
