#pragma once

#include "../engineContext.hpp"

namespace vkh {
	namespace freezeAnimationSys {
		void init(EngineContext& context);
		void cleanup(EngineContext& context);

		void render(EngineContext& context);
	};
}  // namespace lve
