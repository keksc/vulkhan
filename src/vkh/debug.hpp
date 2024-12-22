#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "engineContext.hpp"

#include <fmt/format.h>

using namespace std::string_literals;

namespace vkh {
namespace debug {
extern PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
#ifdef DEBUG_IMPLEMENTATION
PFN_vkSetDebugUtilsObjectNameEXT vkSetDebugUtilsObjectNameEXT;
#endif
inline void init(EngineContext &context) {
#ifdef NDEBUG
  vkSetDebugUtilsObjectNameEXT =
      reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetInstanceProcAddr(
          context.vulkan.instance, "vkSetDebugUtilsObjectNameEXT"));
#endif
}

inline void setObjName(EngineContext &context, std::string name,
                       VkObjectType type, void *handle) {
#ifdef NDEBUG
  VkDebugUtilsObjectNameInfoEXT nameInfo{};
  nameInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT;
  nameInfo.objectType = type;
  nameInfo.objectHandle = reinterpret_cast<uint64_t>(handle);
  nameInfo.pObjectName = name.c_str();

  VkResult result =
      vkSetDebugUtilsObjectNameEXT(context.vulkan.device, &nameInfo);
  if (result != VK_SUCCESS) {
    throw std::runtime_error("Failed to set object name"s + name);
  }
#endif
}

} // namespace debug
} // namespace vkh
