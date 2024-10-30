#pragma once

#include "../engineContext.hpp"

namespace vkh {
	namespace entitySys {
		void init(EngineContext& context, VkDescriptorSetLayout globalSetLayout);
		void cleanup(EngineContext& context);

		void render(EngineContext& context);
    void update(EngineContext& context); // run physics

	};
}  // namespace lve
