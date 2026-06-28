#pragma once

#include <glm/gtc/type_ptr.hpp>
#include <vulkan/vulkan.hpp>

#include "engineContext.hpp"
#include <cstring>

namespace vkh::debug {

inline void setObjName(EngineContext &context, vk::ObjectType type,
                       uint64_t handle, const char *name) {
#ifndef NDEBUG
  vk::DebugUtilsObjectNameInfoEXT nameInfo{type, handle, name};

  VkDebugUtilsObjectNameInfoEXT rawNameInfo =
      static_cast<VkDebugUtilsObjectNameInfoEXT>(nameInfo);
  VkDevice rawDevice = static_cast<VkDevice>(context.vulkan.device);

  context.vulkan.debug.setObjName(rawDevice, &rawNameInfo);
#endif
}

inline void beginLabel(EngineContext &context, vk::CommandBuffer cmd,
                       const char *name, glm::vec4 color) {
#ifndef NDEBUG
  vk::DebugUtilsLabelEXT labelInfo{};
  labelInfo.pLabelName = name;
  std::memcpy(labelInfo.color.data(), glm::value_ptr(color), 4 * sizeof(float));

  VkDebugUtilsLabelEXT rawLabelInfo =
      static_cast<VkDebugUtilsLabelEXT>(labelInfo);
  VkCommandBuffer rawCmd = static_cast<VkCommandBuffer>(cmd);

  context.vulkan.debug.beginLabel(rawCmd, &rawLabelInfo);
#endif
}

inline void endLabel(EngineContext &context, vk::CommandBuffer cmd) {
#ifndef NDEBUG
  VkCommandBuffer rawCmd = static_cast<VkCommandBuffer>(cmd);
  context.vulkan.debug.endLabel(rawCmd);
#endif
}

} // namespace vkh::debug
