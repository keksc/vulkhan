#pragma once

#include <cassert>
#include <cstring>
#include <glm/ext.hpp>
#include <vulkan/vulkan.hpp>

#include "deviceHelpers.hpp"
#include "engineContext.hpp"

namespace vkh {
template <typename T> class Buffer {
public:
  Buffer(EngineContext &context, vk::BufferUsageFlags usage,
         vk::MemoryPropertyFlags memoryProperties,
         unsigned int instanceCount = 1)
      : context{context} {
    // alignmentSize = getAlignment(createInfo.instanceSize, 1);
    instanceSize = sizeof(T);
    bufSize = instanceSize * instanceCount;
    createBuffer(context, bufSize, usage, memoryProperties, buf, memory);
  }

  ~Buffer() {
    unmap();
    if (buf) {
      context.vulkan.device.destroyBuffer(buf);
    }
    if (memory) {
      context.vulkan.device.freeMemory(memory);
    }
  }

  // Disable copy semantics
  Buffer(const Buffer &) = delete;
  Buffer &operator=(const Buffer &) = delete;

  // Enable move semantics (highly recommended for Vulkan wrappers)
  Buffer(Buffer &&other) noexcept : context{other.context} {
    *this = std::move(other);
  }
  Buffer &operator=(Buffer &&other) noexcept {
    if (this != &other) {
      unmap();
      if (buf)
        context.vulkan.device.destroyBuffer(buf);
      if (memory)
        context.vulkan.device.freeMemory(memory);

      mapped = other.mapped;
      buf = other.buf;
      memory = other.memory;
      bufSize = other.bufSize;
      instanceSize = other.instanceSize;
      alignmentSize = other.alignmentSize;

      other.mapped = nullptr;
      other.buf = nullptr;
      other.memory = nullptr;
    }
    return *this;
  }

  void *map(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0) {
    assert(buf && memory && "Called map on buffer before create");
    mapped = context.vulkan.device.mapMemory(memory, offset, size,
                                             vk::MemoryMapFlags(0));
    return mapped;
  }

  void unmap() {
    if (mapped) {
      context.vulkan.device.unmapMemory(memory);
      mapped = nullptr;
    }
  }

  void write(const void *data, vk::DeviceSize size = VK_WHOLE_SIZE,
             vk::DeviceSize offset = 0) {
    assert(mapped && "Cannot copy to unmapped buffer");

    if (size == VK_WHOLE_SIZE) {
      std::memcpy(mapped, data, bufSize);
    } else {
      char *memOffset = static_cast<char *>(mapped);
      memOffset += offset;
      std::memcpy(memOffset, data, size);
    }
  }

  void flush(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0) {
    vk::MappedMemoryRange mappedRange{memory, offset, size};
    if (context.vulkan.device.flushMappedMemoryRanges(1, &mappedRange) !=
        vk::Result::eSuccess)
      throw std::runtime_error("Failed to flush mapped memory range");
  }

  vk::DescriptorBufferInfo descriptorInfo(vk::DeviceSize size = VK_WHOLE_SIZE,
                                          vk::DeviceSize offset = 0) {
    return vk::DescriptorBufferInfo{
        buf,
        offset,
        size,
    };
  }

  void invalidate(vk::DeviceSize size = VK_WHOLE_SIZE,
                  vk::DeviceSize offset = 0) {
    vk::MappedMemoryRange mappedRange{memory, offset, size};
    if (context.vulkan.device.invalidateMappedMemoryRanges(1, &mappedRange) !=
        vk::Result::eSuccess)
      throw std::runtime_error("Failed to invalidate mapped memory range");
  }

  void writeToIndex(void *data, int index);
  void flushIndex(int index);
  vk::DescriptorBufferInfo descriptorInfoForIndex(int index);
  void invalidateIndex(int index);

  operator vk::Buffer &() { return buf; }
  operator const vk::Buffer &() const { return buf; }

  void *getMappedAddr() const { return mapped; }

  template <typename U>
  void copyFromBuffer(Buffer<U> &srcBuffer, vk::DeviceSize size = VK_WHOLE_SIZE,
                      vk::DeviceSize srcOffset = 0,
                      vk::DeviceSize dstOffset = 0) {
    auto cmd = beginSingleTimeCommands(context);
    recordCopyFromBuffer(cmd, srcBuffer, size, srcOffset, dstOffset);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
  }

  template <typename U>
  void recordCopyFromBuffer(vk::CommandBuffer cmdBuffer, Buffer<U> &srcBuffer,
                            vk::DeviceSize size = VK_WHOLE_SIZE,
                            vk::DeviceSize srcOffset = 0,
                            vk::DeviceSize dstOffset = 0) {
    vk::BufferCopy copyRegion{srcOffset, dstOffset,
                              size == VK_WHOLE_SIZE ? bufSize : size};
    cmdBuffer.copyBuffer(srcBuffer, buf, 1, &copyRegion);
  }

  vk::DeviceSize getSize() const { return bufSize; }

private:
  void allocateMemory(vk::MemoryPropertyFlags properties) {
    assert(buf);

    vk::MemoryRequirements memRequirements =
        context.vulkan.device.getBufferMemoryRequirements(buf);

    vk::MemoryAllocateInfo allocInfo{
        memRequirements.size,
        findMemoryType(context, memRequirements.memoryTypeBits, properties)};

    memory = context.vulkan.device.allocateMemory(allocInfo);
  }

  vk::DeviceSize getAlignment(vk::DeviceSize instanceSize,
                              vk::DeviceSize minOffsetAlignment) {
    if (minOffsetAlignment > 0) {
      return (instanceSize + minOffsetAlignment - 1) &
             ~(minOffsetAlignment - 1);
    }
    return instanceSize;
  }

  EngineContext &context;

  void *mapped = nullptr;
  vk::Buffer buf;
  vk::DeviceMemory memory;
  vk::DeviceSize bufSize;
  vk::DeviceSize instanceSize;
  vk::DeviceSize alignmentSize;
};
} // namespace vkh
