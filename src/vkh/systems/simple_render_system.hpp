#pragma once

#include "../engineContext.hpp"

namespace vkh {
	namespace objRenderSys {
		void init(EngineContext& context, VkDescriptorSetLayout globalSetLayout);
		void cleanup(EngineContext& context);

		void renderGameObjects(EngineContext& context);

	};
}  // namespace lve
