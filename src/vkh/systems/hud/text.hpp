#pragma once

#include <glm/glm.hpp>

#include "../../buffer.hpp"
#include "../../image.hpp"
#include "../system.hpp"

namespace vkh {
class TextSys : public System {
public:
  TextSys(EngineContext &context);
  ~TextSys();
  struct Glyph {
    glm::vec2 size;
    glm::vec2 offset;
    glm::vec2 uvOffset;
    glm::vec2 uvExtent;
    float advance;
  };
  struct GlyphRange {
    std::unordered_map<char, Glyph> glyphs;
    float maxSizeY{};
  };
  static GlyphRange glyphRange;
  struct Vertex {
    glm::vec2 position{};
    glm::vec2 uv{};
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

      attributeDescriptions.push_back(
          {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)});
      attributeDescriptions.push_back(
          {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)});

      return attributeDescriptions;
    }
  };
  void render(size_t indicesSize);

  std::unique_ptr<Buffer> vertexBuffer;
  std::unique_ptr<Buffer> indexBuffer;

private:
  void createBuffers();
  void createDescriptors();
  void createPipeline();
  void createGlyphs();
  void createSampler();

  const int maxCharCount = 80;
  const int maxVertexCount = 4 * maxCharCount; // 4 vertices = 1 quad = 1 glyph
  const VkDeviceSize maxVertexSize = sizeof(Vertex) * maxVertexCount;
  const int maxIndexCount = maxCharCount * 6;
  const VkDeviceSize maxIndexSize = sizeof(uint32_t) * 6 * maxCharCount;
  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
  VkDescriptorSet descriptorSet;
  std::unique_ptr<Image> fontAtlas;
  VkSampler sampler;
};
} // namespace vkh
