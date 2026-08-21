#pragma once

#include <cassert>
#include <cstring>
#include <glm/ext.hpp>
#include <vk_mem_alloc.h>
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
    instanceSize = sizeof(T);
    bufSize = instanceSize * instanceCount;
    createBuffer(context, bufSize, usage, memoryProperties, buf, allocation);
  }

  ~Buffer() {
    unmap();
    if (buf) {
      vmaDestroyBuffer(context.vulkan.allocator, buf, allocation);
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
        vmaDestroyBuffer(context.vulkan.allocator, buf, allocation);

      mapped = other.mapped;
      buf = other.buf;
      allocation = other.allocation;
      bufSize = other.bufSize;
      instanceSize = other.instanceSize;
      alignmentSize = other.alignmentSize;

      other.mapped = nullptr;
      other.buf = nullptr;
      other.allocation = nullptr;
    }
    return *this;
  }

  void *map(vk::DeviceSize size = VK_WHOLE_SIZE, vk::DeviceSize offset = 0) {
    assert(buf && allocation && "Called map on buffer before create");
    if (vmaMapMemory(context.vulkan.allocator, allocation, &mapped) !=
        VK_SUCCESS) {
      throw std::runtime_error("failed to map buffer memory via VMA!");
    }
    return mapped;
  }

  void unmap() {
    if (mapped) {
      vmaUnmapMemory(context.vulkan.allocator, allocation);
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
    if (vmaFlushAllocation(context.vulkan.allocator, allocation, offset,
                           size) != VK_SUCCESS) {
      throw std::runtime_error("Failed to flush VMA allocation");
    }
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
    if (vmaInvalidateAllocation(context.vulkan.allocator, allocation, offset,
                                size) != VK_SUCCESS) {
      throw std::runtime_error("Failed to invalidate VMA allocation");
    }
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

  vk::DeviceAddress getDeviceAddress() const {
    vk::BufferDeviceAddressInfo info{buf};
    return context.vulkan.device.getBufferAddress(info);
  }

private:
  EngineContext &context;

  void *mapped = nullptr;
  vk::Buffer buf;
  VmaAllocation allocation = nullptr;
  vk::DeviceSize bufSize;
  vk::DeviceSize instanceSize;
  vk::DeviceSize alignmentSize;
};
} // namespace vkh
