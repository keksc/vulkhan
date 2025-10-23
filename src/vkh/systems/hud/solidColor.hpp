#pragma once

#include "../../image.hpp"
#include "../system.hpp"

namespace vkh {
class SolidColorSys : public System {
public:
  SolidColorSys(EngineContext &context);

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

  std::unique_ptr<Buffer<LineVertex>> linesVertexBuffer;
  std::unique_ptr<Buffer<TriangleVertex>> trianglesVertexBuffer;
  std::unique_ptr<Buffer<uint32_t>> trianglesIndexBuffer;
  unsigned short addTextureFromMemory(unsigned char *pixels, glm::uvec2 size);

  std::vector<Image> images;

private:
  void createBuffers();
  void createDescriptors();
  void createPipelines();
  void updateDescriptors();

  const int maxLineCount = 160;
  const int maxLineVertexCount =
      2 * maxLineCount; // 4 vertices = 1 quad = 1 glyph
  const VkDeviceSize maxLineVertexSize =
      sizeof(LineVertex) * maxLineVertexCount;

  const uint32_t maxTextures = 64;
  const int maxRectCount = 1000;
  const int maxTriangleVertexCount = 4 * maxRectCount; // 4 vertices = 1 quad
  const int maxTriangleIndexCount = 6 * maxRectCount;
  VkDeviceSize maxTriangleVertexSize =
      sizeof(TriangleVertex) * maxTriangleVertexCount;

  std::unique_ptr<GraphicsPipeline> trianglePipeline;
  std::unique_ptr<GraphicsPipeline> linesPipeline;
  std::unique_ptr<DescriptorSetLayout> setLayout;
  VkDescriptorSet set;
};
} // namespace vkh
