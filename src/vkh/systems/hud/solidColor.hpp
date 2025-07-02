#pragma once

#include "../system.hpp"

namespace vkh {
class SolidColorSys : public System {
public:
  SolidColorSys(EngineContext &context);

  struct Vertex {
    glm::vec2 position{};
    glm::vec3 color{};
    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<VkVertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(Vertex);
      bindingDescriptions[0].inputRate = VK_VERTEX_INPUT_RATE_VERTEX;
      return bindingDescriptions;
    }
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<VkVertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(
          0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position));
      attributeDescriptions.emplace_back(
          1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, color));

      return attributeDescriptions;
    }
  };

  void render(size_t lineVerticesSize, size_t triangleIndicesSize);

  std::unique_ptr<Buffer<Vertex>> linesVertexBuffer;
  std::unique_ptr<Buffer<Vertex>> trianglesVertexBuffer;
  std::unique_ptr<Buffer<uint32_t>> trianglesIndexBuffer;

private:
  void createBuffers();
  void createPipeline();

  const int maxLineCount = 160;
  const int maxLineVertexCount = 2 * maxLineCount; // 4 vertices = 1 quad = 1 glyph
  const VkDeviceSize maxLineVertexSize = sizeof(Vertex) * maxLineVertexCount;

  const int maxRectCount = 1000;
  const int maxTriangleVertexCount = 4 * maxRectCount; // 4 vertices = 1 quad
  const int maxTriangleIndexCount = 6 * maxRectCount;
  VkDeviceSize maxTriangleVertexSize = sizeof(Vertex) * maxTriangleVertexCount;

  std::unique_ptr<GraphicsPipeline> trianglePipeline;
  std::unique_ptr<GraphicsPipeline> linesPipeline;
};
} // namespace vkh
