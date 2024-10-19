#pragma once

#include "camera.hpp"
#include "gameObject.hpp"

// lib
#include <vulkan/vulkan.h>

namespace vkh {
	const int MAX_LIGHTS = 10;

	struct GlobalUbo {
		glm::mat4 projection{ 1.f };
		glm::mat4 view{ 1.f };
		glm::mat4 inverseView{ 1.f };
		glm::vec4 ambientLightColor{ 1.f, 1.f, 1.f, .02f };  // w is intensity
		struct {
			glm::vec4 position{}; // ignore w
			glm::vec4 color{}; // w is intensity
		} pointLights[MAX_LIGHTS];
		int numLights;
	};

	struct FrameInfo {
		int frameIndex;
		float frameTime;
		VkCommandBuffer commandBuffer;
		LveCamera& camera;
		VkDescriptorSet globalDescriptorSet;
	};
}  // namespace lve