#include "cleanupVulkan.hpp"
#include <vulkan/vulkan_core.h>

namespace vkh {
void cleanupVulkan(EngineContext &context) {
  vkDestroySampler(context.vulkan.device, context.vulkan.modelSampler, nullptr);
  vkDestroySampler(context.vulkan.device, context.vulkan.fontSampler, nullptr);
  vkDestroyCommandPool(context.vulkan.device, context.vulkan.commandPool,
                       nullptr);
  vkDestroyDevice(context.vulkan.device, nullptr);

  if (context.vulkan.enableValidationLayers) {
    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
        context.vulkan.instance, "vkDestroyDebugUtilsMessengerEXT"))(
        context.vulkan.instance, context.vulkan.debugMessenger, nullptr);
  }

  vkDestroySurfaceKHR(context.vulkan.instance, context.vulkan.surface, nullptr);
  vkDestroyInstance(context.vulkan.instance, nullptr);
}
} // namespace vkh
