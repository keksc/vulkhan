#pragma once

#include "gameObject.hpp"
#include "ecs.hpp"

namespace vkh {
	class Controller : public System {
	public:
		struct KeyStatuses {
			bool moveLeft = false;
			bool moveRight = false;
			bool moveForward = false;
			bool moveBackward = false;
			bool moveUp = false;
			bool moveDown = false;
			bool showDebugInfo = false;
		};

		Controller(EngineContext& context);
		void moveInPlaneXZ(EngineContext& context, float dt, Entity viewerEntity);

		float moveSpeed{ 3.f };
		float lookSpeed{ 1.5f };
	private:
		EngineContext& context;
	};
}  // namespace lve