#pragma once

#include "../engineContext.hpp"

namespace vkh {
	namespace particlesSys {
		void init(EngineContext& context, VkDescriptorSetLayout globalSetLayout);
		void cleanup(EngineContext& context);

		void render(EngineContext& context);
	};
}  // namespace lve