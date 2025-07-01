#include "cleanup.hpp"

#include <vulkan/vulkan_core.h>

#include "buffer.hpp"
#include "descriptors.hpp"

namespace vkh {
void cleanup(EngineContext &context) {
  vkDestroySampler(context.vulkan.device, context.vulkan.defaultSampler,
                   nullptr);
  context.vulkan.sceneDescriptorSetLayout = nullptr;
  context.vulkan.globalDescriptorSetLayout = nullptr;
  context.vulkan.globalDescriptorPool = nullptr;
  context.vulkan.globalDescriptorSets.clear();
  context.vulkan.globalUBOs.clear();

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
