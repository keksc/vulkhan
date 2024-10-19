#pragma once

#include "engineContext.hpp"

namespace vkh {
	void createInstance(EngineContext& context);
	void setupDebugMessenger(EngineContext& context);
	void pickPhysicalDevice(EngineContext& context);
	void createLogicalDevice(EngineContext& context);
	void createCommandPool(EngineContext& context);
}