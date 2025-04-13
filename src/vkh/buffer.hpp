#pragma once

#include <vulkan/vulkan_core.h>

#include "deviceHelpers.hpp"
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

  Buffer(Buffer &&other) noexcept
      : context(other.context), mapped(other.mapped), buf(other.buf),
        memory(other.memory), bufSize(other.bufSize),
        instanceSize(other.instanceSize), alignmentSize(other.alignmentSize) {
    other.mapped = nullptr;
    other.buf = VK_NULL_HANDLE;
    other.memory = VK_NULL_HANDLE;
  }

  void *map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
  void unmap();

  void write(const void *data, VkDeviceSize size = VK_WHOLE_SIZE,
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

  void copyToMapped(const void *srcData, VkDeviceSize size,
                    void *destAddr = nullptr) const;
  void *getMappedAddr() const { return mapped; }

  void copyFromBuffer(VkCommandBuffer cmdBuffer, Buffer &srcBuffer,
                      VkDeviceSize size = VK_WHOLE_SIZE,
                      VkDeviceSize srcOffset = 0, VkDeviceSize dstOffset = 0);

  Buffer(EngineContext &context, VkDeviceSize size,
         VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
         VkMemoryPropertyFlags memoryProperties =
             VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
         VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE)
      : context{context} {
    // Create the buffer
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = sharingMode;
    bufferInfo.flags = 0;

    vkCreateBuffer(context.vulkan.device, &bufferInfo, nullptr, &buf);
    allocateMemory(memoryProperties);
    vkBindBufferMemory(context.vulkan.device, buf, memory, 0);
  }

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
