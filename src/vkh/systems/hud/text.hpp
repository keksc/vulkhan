#pragma once

#include <glm/glm.hpp>
#include <unordered_map>
#include <vulkan/vulkan.hpp>

#include "../../buffer.hpp"
#include "../../image.hpp"
#include "../system.hpp"

#include <memory>
#include <vector>

namespace vkh {

class GraphicsPipeline;

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
    glm::vec3 position{};
    glm::vec3 color{};
    glm::vec2 uv{};

    static std::vector<vk::VertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(Vertex);
      bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
      return bindingDescriptions;
    }

    static std::vector<vk::VertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(Vertex, position));
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(Vertex, color));
      attributeDescriptions.emplace_back(2, 0, vk::Format::eR32G32Sfloat,
                                         offsetof(Vertex, uv));

      return attributeDescriptions;
    }
  };

  void render(size_t indicesSize);
  void ensureCapacity(size_t vertexCount, size_t indexCount);

  std::unique_ptr<Buffer<Vertex>> vertexBuffer;
  std::unique_ptr<Buffer<uint32_t>> indexBuffer;

private:
  void createBuffers();
  void createDescriptors();
  void createPipeline();
  void createGlyphs();

  int maxVertexCount = 4000;
  int maxIndexCount = 6000;

  std::unique_ptr<GraphicsPipeline> pipeline;
  vk::DescriptorSetLayout setLayout;
  vk::DescriptorSet set;
  std::unique_ptr<Image> fontAtlas;
};

} // namespace vkh
