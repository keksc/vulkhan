#include "buffer.hpp"

#include <fmt/core.h>
#include <fmt/format.h>

#include "deviceHelpers.hpp"

#include <cassert>
#include <cstring>

namespace vkh {
void Buffer::copyFromBuffer(VkCommandBuffer cmdBuffer, Buffer &srcBuffer,
                            VkDeviceSize size, VkDeviceSize srcOffset,
                            VkDeviceSize dstOffset) {
  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = srcOffset;
  copyRegion.dstOffset = dstOffset;
  copyRegion.size = size == VK_WHOLE_SIZE ? bufSize : size;
  vkCmdCopyBuffer(cmdBuffer, srcBuffer.buf, buf, 1, &copyRegion);
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
VkDeviceSize Buffer::getAlignment(VkDeviceSize instanceSize,
                                  VkDeviceSize minOffsetAlignment) {
  if (minOffsetAlignment > 0) {
    return (instanceSize + minOffsetAlignment - 1) & ~(minOffsetAlignment - 1);
  }
  return instanceSize;
}
Buffer::Buffer(EngineContext &context, const BufferCreateInfo &createInfo)
    : context{context} {
  // alignmentSize = getAlignment(createInfo.instanceSize, 1);
  bufSize = createInfo.instanceSize * createInfo.instanceCount;
  instanceSize = createInfo.instanceSize;
  createBuffer(context, bufSize, createInfo.usage, createInfo.memoryProperties,
               buf, memory);
}
Buffer::~Buffer() {
  unmap();
  vkDestroyBuffer(context.vulkan.device, buf, nullptr);
  vkFreeMemory(context.vulkan.device, memory, nullptr);
}
void *Buffer::map(VkDeviceSize size, VkDeviceSize offset) {
  assert(buf && memory && "Called map on buffer before create");
  vkMapMemory(context.vulkan.device, memory, offset, size, 0, &mapped);
  return mapped;
}
void Buffer::unmap() {
  if (mapped) {
    vkUnmapMemory(context.vulkan.device, memory);
    mapped = nullptr;
  }
}
void Buffer::write(const void *data, VkDeviceSize size, VkDeviceSize offset) {
  assert(mapped && "Cannot copy to unmapped buffer");

  if (size == VK_WHOLE_SIZE) {
    memcpy(mapped, data, bufSize);
  } else {
    char *memOffset = (char *)mapped;
    memOffset += offset;
    memcpy(memOffset, data, size);
  }
}
VkResult Buffer::flush(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = memory;
  mappedRange.offset = offset;
  mappedRange.size = size;
  return vkFlushMappedMemoryRanges(context.vulkan.device, 1, &mappedRange);
}
VkResult Buffer::invalidate(VkDeviceSize size, VkDeviceSize offset) {
  VkMappedMemoryRange mappedRange = {};
  mappedRange.sType = VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
  mappedRange.memory = memory;
  mappedRange.offset = offset;
  mappedRange.size = size;
  return vkInvalidateMappedMemoryRanges(context.vulkan.device, 1, &mappedRange);
}
VkDescriptorBufferInfo Buffer::descriptorInfo(VkDeviceSize size,
                                              VkDeviceSize offset) {
  return VkDescriptorBufferInfo{
      buf,
      offset,
      VK_WHOLE_SIZE,
  };
}
void Buffer::copyToMapped(const void *data, VkDeviceSize size,
                          void *destAddr) const {
  void *dest = destAddr == nullptr ? mapped : destAddr;
  memcpy(dest, data, static_cast<size_t>(size));
}
} // namespace vkh
