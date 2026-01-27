#include "cleanup.hpp"

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"
#include "descriptors.hpp"

namespace vkh {
void cleanup(EngineContext &context) {
  vkDestroyDescriptorSetLayout(
      context.vulkan.device, context.vulkan.globalDescriptorSetLayout, nullptr);
  vkDestroySampler(context.vulkan.device, context.vulkan.defaultSampler,
                   nullptr);
  context.vulkan.globalDescriptorAllocator = nullptr;
  context.vulkan.globalDescriptorSets.clear();
  context.vulkan.globalUBOs.clear();

  vkDestroyCommandPool(context.vulkan.device, context.vulkan.commandPool,
                       nullptr);
  vkDestroyDevice(context.vulkan.device, nullptr);

#ifndef NDEBUG
  reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
      context.vulkan.instance, "vkDestroyDebugUtilsMessengerEXT"))(
      context.vulkan.instance, context.vulkan.debugMessenger, nullptr);
#endif

  vkDestroySurfaceKHR(context.vulkan.instance, context.vulkan.surface, nullptr);
  vkDestroyInstance(context.vulkan.instance, nullptr);
}
} // namespace vkh
