#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

struct VkDetails {
	VkInstance inst;
	VkDebugUtilsMessengerEXT dbgMessenger;
};