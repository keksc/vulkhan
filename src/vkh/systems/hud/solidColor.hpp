#pragma once

#include <filesystem>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

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

    static std::vector<vk::VertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(TriangleVertex);
      bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
      return bindingDescriptions;
    }

    static std::vector<vk::VertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(TriangleVertex, position));
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32Sfloat,
                                         offsetof(TriangleVertex, uv));
      attributeDescriptions.emplace_back(2, 0, vk::Format::eR32Sint,
                                         offsetof(TriangleVertex, texIndex));
      return attributeDescriptions;
    }
  };

  struct LineVertex {
    glm::vec3 position{};
    glm::vec3 color{};

    static std::vector<vk::VertexInputBindingDescription>
    getBindingDescriptions() {
      std::vector<vk::VertexInputBindingDescription> bindingDescriptions(1);
      bindingDescriptions[0].binding = 0;
      bindingDescriptions[0].stride = sizeof(LineVertex);
      bindingDescriptions[0].inputRate = vk::VertexInputRate::eVertex;
      return bindingDescriptions;
    }

    static std::vector<vk::VertexInputAttributeDescription>
    getAttributeDescriptions() {
      std::vector<vk::VertexInputAttributeDescription> attributeDescriptions{};

      attributeDescriptions.emplace_back(0, 0, vk::Format::eR32G32B32Sfloat,
                                         offsetof(LineVertex, position));
      attributeDescriptions.emplace_back(1, 0, vk::Format::eR32G32B32Sfloat,
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
  vk::DescriptorSetLayout texturesSetLayout;
  vk::DescriptorSet texturesSet;
};

} // namespace vkh
