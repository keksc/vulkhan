#pragma once

#include "../engineContext.hpp"

namespace vkh {
	namespace fontSys {
		void init(EngineContext& context);
		void cleanup(EngineContext& context);

		void render(EngineContext& context);
	};
}  // namespace lve