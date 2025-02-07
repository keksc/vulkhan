#pragma once

#include <vulkan/vulkan_core.h>

#include "engineContext.hpp"

namespace vkh {
struct BufferCreateInfo {
  VkDeviceSize instanceSize;
  uint32_t instanceCount = 1;
  VkBufferUsageFlags usage;
  VkMemoryPropertyFlags memoryProperties;
};
class Buffer {
public:
  Buffer(EngineContext &context, const BufferCreateInfo &createInfo);
  ~Buffer();

  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  void map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void unmap();

  void write(void *data, VkDeviceSize size = VK_WHOLE_SIZE,
             VkDeviceSize offset = 0);
  VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  VkDescriptorBufferInfo descriptorInfo(VkDeviceSize size = VK_WHOLE_SIZE,
                                        VkDeviceSize offset = 0);
  VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE,
                      VkDeviceSize offset = 0);

  void writeToIndex(void *data, int index);
  VkResult flushIndex(int index);
  VkDescriptorBufferInfo descriptorInfoForIndex(int index);
  VkResult invalidateIndex(int index);

  operator VkBuffer() { return buf; }

private:
  static VkDeviceSize getAlignment(VkDeviceSize instanceSize,
                                   VkDeviceSize minOffsetAlignment);

  EngineContext &context;

  void *mapped = nullptr;
  VkBuffer buf;
  VkDeviceMemory memory;
  VkDeviceSize bufSize;
  VkDeviceSize instanceSize;
  VkDeviceSize alignmentSize;
};
} // namespace vkh
