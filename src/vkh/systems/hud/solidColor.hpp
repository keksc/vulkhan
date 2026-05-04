#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include "../system.hpp"

#include <memory>
#include <vector>

namespace vkh {
template <typename T> class Buffer;
class GraphicsPipeline;
class Image;
class SolidColorSys : public System {
public:
  SolidColorSys(EngineContext &context);
  ~SolidColorSys();

  struct TriangleVertex {
    glm::vec3 position{};
    glm::vec2 uv{};
    int texIndex{0};
    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(TriangleVertex);
      bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescriptions;
    }
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(TriangleVertex, position));
      attributeDescriptions.emplace_back(1, 0, VK_FORMAT_R32G32_SFLOAT,
                                         offsetof(TriangleVertex, uv));
      attributeDescriptions.emplace_back(2, 0, VK_FORMAT_R32_SINT,
                                         offsetof(TriangleVertex, texIndex));
      return attributeDescriptions;
    }
  };

  struct LineVertex {
    glm::vec3 position{};
    glm::vec3 color{};
    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(LineVertex);
      bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescriptions;
    }
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(LineVertex, position));
      attributeDescriptions.emplace_back(1, 0, VK_FORMAT_R32G32B32_SFLOAT,
                                         offsetof(LineVertex, color));

      return attributeDescriptions;
    }
  };

  void render(size_t lineVerticesSize, size_t triangleIndicesSize);
  void ensureLineCapacity(size_t vertexCount, size_t indexCount);
  void ensureTriangleCapacity(size_t vertexCount, size_t indexCount);

  std::unique_ptr<Buffer<LineVertex>> linesVertexBuffer;
  std::unique_ptr<Buffer<uint32_t>> linesIndexBuffer;
  std::unique_ptr<Buffer<TriangleVertex>> trianglesVertexBuffer;
  std::unique_ptr<Buffer<uint32_t>> trianglesIndexBuffer;
  size_t addTextureFromPNGMemory(void *data, size_t size);
  size_t addTextureFromFile(std::filesystem::path path);

  std::vector<Image> images;

private:
  void createBuffers();
  void createDescriptors();
  void createPipelines();
  void updateDescriptors();

  int maxLineVertexCount = 400;
  int maxLineIndexCount = 400;

  const uint32_t maxTextures = 256;
  int maxTriangleVertexCount = 4000;
  int maxTriangleIndexCount = 6000;

  std::unique_ptr<GraphicsPipeline> trianglePipeline;
  std::unique_ptr<GraphicsPipeline> linesPipeline;
  VkDescriptorSetLayout texturesSetLayout;
  VkDescriptorSet texturesSet;
};
} // namespace vkh
