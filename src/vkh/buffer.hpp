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

  void map(VkDeviceSize size = VK_WHOLE_SIZE, VkDeviceSize offset = 0);
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

  Buffer(
      EngineContext &context, VkDeviceSize size,
      VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
      VkMemoryPropertyFlags memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
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
/*
    class Buffer
    {
    public:
        Buffer(const Device& device);

        ~Buffer();

        Buffer(Buffer&& other)
            : m_Device(other.m_Device),
              m_Buffer(other.m_Buffer),
              m_BufferMemory(other.m_BufferMemory),
              m_MapAddr(other.m_MapAddr),
              m_BufferView(other.m_BufferView)
        {
            other.m_Buffer = VK_NULL_HANDLE;
            other.m_BufferMemory = VK_NULL_HANDLE;
            other.m_MapAddr = nullptr;
            other.m_BufferView = VK_NULL_HANDLE;
        }

        operator VkBuffer() const { return m_Buffer; }
        VkBuffer GetBuffer() const { return m_Buffer; }

        VkDescriptorBufferInfo GetDescriptor(VkDeviceSize offset = 0,
                                             VkDeviceSize range = VK_WHOLE_SIZE
        ) const {
            VKP_ASSERT(m_Buffer != VK_NULL_HANDLE);
            return VkDescriptorBufferInfo{
                .buffer = m_Buffer,
                .offset = offset,
                .range = range
            };
        }
    
        void Create(VkDeviceSize size, 
                    VkBufferUsageFlags usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                    VkMemoryPropertyFlags properties = 
                        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                        VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                    VkSharingMode sharingMode = VK_SHARING_MODE_EXCLUSIVE);

        VkResult Fill(const void* data, VkDeviceSize size);

        void StageCopy(VkBuffer stagingBuffer, 
                       const VkBufferCopy *pRegion,
                       VkCommandBuffer commandBuffer);

        VkResult Map(VkDeviceSize size = VK_WHOLE_SIZE,
                     VkDeviceSize offset = 0);
        VkResult Map(void** ppData,
                     VkDeviceSize size = VK_WHOLE_SIZE,
                     VkDeviceSize offset = 0);

        void* GetMappedAddress() const { return m_MapAddr; }

        void Unmap();

        void CopyToMapped(const void* srcData,
                          VkDeviceSize size,
                          void* destAddr = nullptr) const;

        void FlushMappedRange(VkDeviceSize size = VK_WHOLE_SIZE,
                              VkDeviceSize offset = 0) const;
        
        void CreateBufferView(VkFormat format,
                              VkFormatFeatureFlags reqFeatures,
                              VkDeviceSize offset = 0,
                              VkDeviceSize range = VK_WHOLE_SIZE);

        void DestoryBufferView();

        VkBufferView GetBufferView() const { return m_BufferView; };

    private:
        void CreateBuffer(VkDeviceSize size, 
                          VkBufferUsageFlags usage, 
                          VkSharingMode sharingMode);

        void AllocateMemory(VkMemoryPropertyFlags properties);

        void Bind(VkDeviceSize offset = 0) const;

        void Destroy();

    private:
        const Device& m_Device;

        VkBuffer       m_Buffer       { VK_NULL_HANDLE };
        VkDeviceMemory m_BufferMemory { VK_NULL_HANDLE };

        // Address of the mapped memory region
        void*          m_MapAddr       { nullptr }; 

        // Buffer view - for texel buffers
        VkBufferView m_BufferView{ VK_NULL_HANDLE };
    };
*/
