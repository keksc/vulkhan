#pragma once

#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan_core.h>

#include "engineContext.hpp"

namespace vkh::debug {
inline void setObjName(EngineContext &context, VkObjectType type,
                       uint64_t handle, const char *name) {
#ifndef NDEBUG
  VkDebugUtilsObjectNameInfoEXT nameInfo{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT,
      .objectType = type,
      .objectHandle = reinterpret_cast<uint64_t>(handle),
      .pObjectName = name};
  context.vulkan.debug.setObjName(context.vulkan.device, &nameInfo);
#endif
}
inline void beginLabel(EngineContext& context, VkCommandBuffer cmd, const char *name, glm::vec4 color) {
#ifndef NDEBUG
  VkDebugUtilsLabelEXT labelInfo{
      .sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_LABEL_EXT, .pLabelName = name};
  std::memcpy(labelInfo.color, glm::value_ptr(color), 4 * sizeof(float));
  context.vulkan.debug.beginLabel(cmd, &labelInfo);
#endif
}
inline void endLabel(EngineContext& context, VkCommandBuffer cmd) {
#ifndef NDEBUG
  context.vulkan.debug.endLabel(cmd);
#endif
}
} // namespace vkh::debug
