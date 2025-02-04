#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "buffer.hpp"
#include "engineContext.hpp"
#include "image.hpp"

#include <memory>
#include <string>
#include <vector>
#include <filesystem>

namespace vkh {
struct ModelCreateInfo;
class Model {
public:
  struct Vertex {
    glm::vec3 position{};
    glm::vec3 normal{};
    glm::vec2 uv{};

    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions();
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions();

    bool operator==(const Vertex &other) const {
      return position == other.position && normal == other.normal &&
             uv == other.uv;
    }
  };
  void loadModel(const std::filesystem::path &path);

  Model(EngineContext &context, const ModelCreateInfo &createInfo);

  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  void bind(EngineContext &context, VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout);
  void draw(VkCommandBuffer commandBuffer);

private:
  void createVertexBuffer(const std::vector<Vertex> &vertices);
  void createIndexBuffer(const std::vector<uint32_t> &indices);
  inline void createBuffers(const std::vector<Vertex> &vertices,
                                   const std::vector<uint32_t> &indices) {
    createVertexBuffer(vertices);
    createIndexBuffer(indices);
  }
  void createDescriptors();

  EngineContext &context;

  std::unique_ptr<Buffer> vertexBuffer;
  uint32_t vertexCount;

  bool hasIndexBuffer = false;
  std::unique_ptr<Buffer> indexBuffer;
  uint32_t indexCount;

  bool enableTexture = true;

  std::unique_ptr<Image> texture;
  VkDescriptorSet textureDescriptorSet;
};
struct ModelCreateInfo {
  std::string filepath;
  std::vector<Model::Vertex> vertices;
  std::vector<uint32_t> indices;
};
} // namespace vkh
