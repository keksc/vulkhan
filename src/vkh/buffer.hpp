#pragma once

#include <cassert>
#include <cstring>
#include <glm/ext.hpp>
#include <vulkan/vulkan_core.h>

#include "deviceHelpers.hpp"
#include "engineContext.hpp"

namespace vkh {
template <typename T> class Buffer {
public:
  Buffer(EngineContext &context, VkBufferUsageFlags usage,
         VkMemoryPropertyFlags memoryProperties, unsigned int instanceCount = 1)
      : context{context} {
    // alignmentSize = getAlignment(createInfo.instanceSize, 1);
    instanceSize = sizeof(T);
    bufSize = instanceSize * instanceCount;
    createBuffer(context, bufSize, usage, memoryProperties, buf, memory);
  }
  ~Buffer() {
    unmap();
    vkDestroyBuffer(context.vulkan.device, buf, nullptr);
    vkFreeMemory(context.vulkan.device, memory, nullptr);
  }

  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  void *map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0) {
    assert(buf && memory && "Called map on buffer before create");
    vkMapMemory(context.vulkan.device, memory, offset, size, 0, &mapped);
    return mapped;
  }
  void unmap() {
    if (mapped) {
      vkUnmapMemory(context.vulkan.device, memory);
      mapped = nullptr;
    }
  }

  void write(const void *data, VkDeviceSize size = VK_WHOLE_SIZE,
             VkDeviceSize offset = 0) {
    assert(mapped && "Cannot copy to unmapped buffer");

    if (size == VK_WHOLE_SIZE) {
      memcpy(mapped, data, bufSize);
    } else {
      char *memOffset = (char *)mapped;
      memOffset += offset;
      memcpy(memOffset, data, size);
    }
  }
  VkResult flush(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0) {
    VkMappedMemoryRange mappedRange = {};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.memory = memory;
    mappedRange.offset = offset;
    mappedRange.size = size;
    return vkFlushMappedMemoryRanges(context.vulkan.device, 1, &mappedRange);
  }
  VkDescriptorBufferInfo descriptorInfo(VkDeviceSize size = VK_WHOLE_SIZE,
                                        VkDeviceSize offset = 0) {
    return VkDescriptorBufferInfo{
        buf,
        offset,
        VK_WHOLE_SIZE,
    };
  }
  VkResult invalidate(VkDeviceSize size = VK_WHOLE_SIZE,
                      VkDeviceSize offset = 0) {
    VkMappedMemoryRange mappedRange = {};
    mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
    mappedRange.memory = memory;
    mappedRange.offset = offset;
    mappedRange.size = size;
    return vkInvalidateMappedMemoryRanges(context.vulkan.device, 1,
                                          &mappedRange);
  }

  void writeToIndex(void *data, int index);
  VkResult flushIndex(int index);
  VkDescriptorBufferInfo descriptorInfoForIndex(int index);
  VkResult invalidateIndex(int index);

  operator VkBuffer &() { return buf; }
  operator VkBuffer *() { return &buf; }

  void *getMappedAddr() const { return mapped; }

  template<typename U>
  void copyFromBuffer(Buffer<U> &srcBuffer, VkDeviceSize size = VK_WHOLE_SIZE,
                      VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0) {
    auto cmd = beginSingleTimeCommands(context);
    recordCopyFromBuffer(cmd, srcBuffer, size, srcOffset, dstOffset);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  }
  template<typename U>
  void recordCopyFromBuffer(VkCommandBuffer cmdBuffer, Buffer<U> &srcBuffer,
                            VkDeviceSize size = VK_WHOLE_SIZE,
                            VkDeviceSize srcOffset = 0,
                            VkDeviceSize dstOffset = 0) {
    VkBufferCopy copyRegion{};
    copyRegion.srcOffset = srcOffset;
    copyRegion.dstOffset = dstOffset;
    copyRegion.size = size == VK_WHOLE_SIZE ? bufSize : size;
    vkCmdCopyBuffer(cmdBuffer, srcBuffer, buf, 1, &copyRegion);
  }

private:
  void allocateMemory(VkMemoryPropertyFlags properties) {
    assert(buf != VK_NULL_HANDLE);

    VkMemoryRequirements memRequirements;
    vkGetBufferMemoryRequirements(context.vulkan.device, buf, &memRequirements);

    VkMemoryAllocateInfo allocInfo{};
    allocInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocInfo.allocationSize = memRequirements.size;
    allocInfo.memoryTypeIndex =
        findMemoryType(context, memRequirements.memoryTypeBits, properties);

    vkAllocateMemory(context.vulkan.device, &allocInfo, nullptr, &memory);
  }
  /**
   * Returns the minimum instance size required to be compatible with devices
   * minOffsetAlignment
   *
   * @param instanceSize The size of an instance
   * @param minOffsetAlignment The minimum required alignment, in bytes, for the
   * offset member (eg minUniformBufferOffsetAlignment)
   *
   * @return VkResult of the buffer mapping call
   */
  VkDeviceSize getAlignment(VkDeviceSize instanceSize,
                            VkDeviceSize minOffsetAlignment) {
    if (minOffsetAlignment > 0) {
      return (instanceSize + minOffsetAlignment - 1) &
             ~(minOffsetAlignment - 1);
    }
    return instanceSize;
  }

  EngineContext &context;

  void *mapped = nullptr;
  VkBuffer buf;
  VkDeviceMemory memory;
  VkDeviceSize bufSize;
  VkDeviceSize instanceSize;
  VkDeviceSize alignmentSize;
};
} // namespace vkh
