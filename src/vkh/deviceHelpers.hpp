#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "engineContext.hpp"

#include <vector>

namespace vkh {
	struct SwapChainSupportDetails {
		VkSurfaceCapabilitiesKHR capabilities;
		std::vector<VkSurfaceFormatKHR> formats;
		std::vector<VkPresentModeKHR> presentModes;
	};
	struct QueueFamilyIndices {
		uint32_t graphicsFamily;
		uint32_t presentFamily;
		bool graphicsFamilyHasValue = false;
		bool presentFamilyHasValue = false;
		bool isComplete() { return graphicsFamilyHasValue && presentFamilyHasValue; }
	};
	SwapChainSupportDetails querySwapChainSupport(EngineContext& context, VkPhysicalDevice device);
	inline SwapChainSupportDetails getSwapChainSupport(EngineContext& context) { return querySwapChainSupport(context, context.vulkan.physicalDevice); }
	QueueFamilyIndices findQueueFamilies(EngineContext& context, VkPhysicalDevice device);
	inline QueueFamilyIndices findPhysicalQueueFamilies(EngineContext& context) { return findQueueFamilies(context, context.vulkan.physicalDevice); }
	void createImageWithInfo(
		EngineContext& context,
		const VkImageCreateInfo& imageInfo,
		VkMemoryPropertyFlags properties,
		VkImage& image,
		VkDeviceMemory& imageMemory);
	VkFormat findSupportedFormat(
		EngineContext& context, const std::vector<VkFormat>& candidates, VkImageTiling tiling, VkFormatFeatureFlags features);
	void createBuffer(
		EngineContext& context,
		VkDeviceSize size,
		VkBufferUsageFlags usage,
		VkMemoryPropertyFlags properties,
		VkBuffer& buffer,
		VkDeviceMemory& bufferMemory);
	void copyBuffer(EngineContext& context, VkBuffer srcBuffer, VkBuffer dstBuffer, VkDeviceSize size);
}