#pragma once

#include <fmt/core.h>
#include <memory>
#include <vector>
#include <vulkan/vulkan_core.h>

#include "../../buffer.hpp"
#include "../../engineContext.hpp"

/**
 * @brief TODO use case
 */
namespace vkh {
template <class T> class Mesh {
public:
  Mesh();

  Mesh(EngineContext &context, VkDeviceSize maxVerticesSize,
       VkDeviceSize maxIndicesSize) {
    CreateBuffers(context, maxVerticesSize, maxIndicesSize);
  }

  ~Mesh();

  void SetVertices(const std::vector<T> &kVertices) {
    m_Vertices = kVertices;
    m_LatestBuffersOnDevice = false;
  }
  void SetVertices(std::vector<T> &&vertices) {
    m_Vertices = vertices;
    m_LatestBuffersOnDevice = false;
  }

  void SetIndices(const std::vector<uint32_t> &kIndices) {
    m_Indices = kIndices;
    m_LatestBuffersOnDevice = false;
  }
  void SetIndices(std::vector<uint32_t> &&indices) {
    m_Indices = indices;
    m_LatestBuffersOnDevice = false;
  }

  /**
   * @pre Set vertices and indices
   * @brief Stages a copy of the set vertices and indices to the device buffers.
   * @param stagingBuffer Reference to a new buffer object, will be used
   *  to transfer indices to index buffer, must be mapped and big enough
   *  to store set vertices and indices sequentially in memory.
   * @param cmdBuffer Command buffer in recording state to record copy cmd to
   */
  bool UpdateDeviceBuffers(Buffer &stagingBuffer, VkCommandBuffer cmdBuffer) {
    if (m_LatestBuffersOnDevice)
      return false;

    StageCopyVerticesToVertexBuffer(stagingBuffer, cmdBuffer);
    StageCopyIndicesToIndexBuffer(stagingBuffer, cmdBuffer);

    m_LatestBuffersOnDevice = true;
    return true;
  }

  /**
   * @pre Uploaded data to vertex and index buffers
   */
  void Render(VkCommandBuffer cmdBuffer) {
    BindBuffers(cmdBuffer);
    DrawIndexed(cmdBuffer, GetIndexCount());
  }

  /**
   * @brief Creates vertex and index buffers on the device with
   *  reserved size according to the set vertices and indices.
   * @param verticesSize Allocated size for vertex buffer
   * @param indicesSize Allocated size for index buffer
   */
  void CreateBuffers(EngineContext &context, VkDeviceSize verticesSize,
                     VkDeviceSize indicesSize);

  void UpdateDeviceVertexBuffer(Buffer &stagingBuffer,
                                VkCommandBuffer cmdBuffer) {
    StageCopyVerticesToVertexBuffer(stagingBuffer, cmdBuffer);
  }
  void UpdateDeviceIndexBuffer(Buffer &stagingBuffer,
                               VkCommandBuffer cmdBuffer) {
    StageCopyIndicesToIndexBuffer(stagingBuffer, cmdBuffer);
  }

  const std::vector<T> &GetVertices() const { return m_Vertices; }
  std::vector<T> &GetVertices() { return m_Vertices; }

  uint32_t GetIndexCount() const { return m_Indices.size(); }
  uint32_t GetVertexCount() const { return m_Vertices.size(); }

  VkDeviceSize GetVerticesSize() const { return sizeof(T) * GetVertexCount(); }
  VkDeviceSize GetIndicesSize() const {
    return sizeof(uint32_t) * GetIndexCount();
  }

private:
  void InitVertices();
  void InitIndices();

  void StageCopyVerticesToVertexBuffer(Buffer &stagingBuffer,
                                       VkCommandBuffer cmdBuffer);
  void StageCopyIndicesToIndexBuffer(Buffer &stagingBuffer,
                                     VkCommandBuffer cmdBuffer);

  void BindBuffers(VkCommandBuffer cmdBuffer) const;
  void DrawIndexed(VkCommandBuffer cmdBuffer, const uint32_t kIndexCount) const;

private:
  std::vector<T> m_Vertices;
  std::vector<uint32_t> m_Indices;

  // On device buffers
  std::unique_ptr<Buffer> m_VertexBuffer{nullptr};
  std::unique_ptr<Buffer> m_IndexBuffer{nullptr};

  bool m_LatestBuffersOnDevice{false};
};

// TODO into implementation header file

template <class T> Mesh<T>::Mesh() {}

template <class T> Mesh<T>::~Mesh() {}

template <class T>
void Mesh<T>::CreateBuffers(EngineContext &context, VkDeviceSize verticesSize,
                            VkDeviceSize indicesSize) {
  m_VertexBuffer.reset(new Buffer(context, verticesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
  m_IndexBuffer.reset(new Buffer(context, indicesSize, VK_BUFFER_USAGE_TRANSFER_DST_BIT | VK_BUFFER_USAGE_INDEX_BUFFER_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT));
}

template <class T>
void Mesh<T>::StageCopyVerticesToVertexBuffer(Buffer &stagingBuffer,
                                              VkCommandBuffer cmdBuffer) {
  assert(GetVertexCount() > 0);

  const VkDeviceSize kVerticesSize = GetVerticesSize();
  stagingBuffer.write(m_Vertices.data(), kVerticesSize);

  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = 0;
  copyRegion.dstOffset = 0;
  copyRegion.size = kVerticesSize;

  vkCmdCopyBuffer(cmdBuffer, stagingBuffer, *m_VertexBuffer, 1, &copyRegion);
}

template <class T>
void Mesh<T>::StageCopyIndicesToIndexBuffer(Buffer &stagingBuffer,
                                            VkCommandBuffer cmdBuffer) {
  assert(GetIndexCount() > 0);

  const VkDeviceSize kVerticesSize = GetVerticesSize();
  const VkDeviceSize kIndicesSize = GetIndicesSize();

  stagingBuffer.write(m_Indices.data(), kIndicesSize, kVerticesSize);

  VkBufferCopy copyRegion{};
  copyRegion.srcOffset = kVerticesSize;
  copyRegion.dstOffset = 0;
  copyRegion.size = kIndicesSize;

  vkCmdCopyBuffer(cmdBuffer, stagingBuffer, *m_IndexBuffer, 1, &copyRegion);
}

template <class T> void Mesh<T>::BindBuffers(VkCommandBuffer cmdBuffer) const {
  const VkBuffer kVertexBuffers[] = {*m_VertexBuffer};
  const VkDeviceSize kOffsets[] = {0};
  const uint32_t kFirstBinding = 0, kBindingCount = 1;

  vkCmdBindVertexBuffers(cmdBuffer, kFirstBinding, kBindingCount,
                         kVertexBuffers, kOffsets);

  const VkDeviceSize kIndexOffset = 0;
  vkCmdBindIndexBuffer(cmdBuffer, *m_IndexBuffer, kIndexOffset,
                       VK_INDEX_TYPE_UINT32);
}

template <class T>
void Mesh<T>::DrawIndexed(VkCommandBuffer cmdBuffer,
                          const uint32_t kIndexCount) const {
  const uint32_t kInstanceCount = 1;
  const uint32_t kFirstIndex = 0, kFirstInstance = 0;
  const uint32_t kVertexOffset = 0;

  vkCmdDrawIndexed(cmdBuffer, kIndexCount, kInstanceCount, kFirstIndex,
                   kVertexOffset, kFirstInstance);
}
} // namespace vkh
