#include "cleanup.hpp"

#include <vulkan/vulkan.hpp>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "pipeline.hpp"

namespace vkh {

void cleanup(EngineContext &context) {
  if (context.vulkan.device) {
    Pipeline::saveAndCleanCache(context);
    context.vulkan.device.destroyDescriptorSetLayout(
        context.vulkan.globalDescriptorSetLayout, nullptr);
    context.vulkan.device.destroySampler(context.vulkan.defaultSampler,
                                         nullptr);

    context.vulkan.globalDescriptorAllocator = nullptr;
    context.vulkan.globalDescriptorSets.clear();
    context.vulkan.globalUBOs.clear();

    context.vulkan.device.destroyCommandPool(context.vulkan.commandPool,
                                             nullptr);
    context.vulkan.device.destroy(nullptr);
  }

#ifndef NDEBUG
  if (context.vulkan.instance && context.vulkan.debugMessenger) {
    auto vkDestroyDebugUtilsMessengerEXT =
        reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
            context.vulkan.instance.getProcAddr(
                "vkDestroyDebugUtilsMessengerEXT"));
    if (vkDestroyDebugUtilsMessengerEXT) {
      vkDestroyDebugUtilsMessengerEXT(
          static_cast<VkInstance>(context.vulkan.instance),
          context.vulkan.debugMessenger, nullptr);
    }
  }
#endif

  if (context.vulkan.instance) {
    if (context.vulkan.surface) {
      context.vulkan.instance.destroySurfaceKHR(context.vulkan.surface,
                                                nullptr);
    }
    context.vulkan.instance.destroy(nullptr);
  }
}

} // namespace vkh
