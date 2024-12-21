#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "buffer.hpp"
#include "engineContext.hpp"

#include <memory>
#include <string>
#include <vector>

namespace vkh {
class Model {
public:
  struct Vertex {
    glm::vec3 position{};
    glm::vec3 color{};
    glm::vec3 normal{};
    glm::vec2 uv{};

    static std::vector<VkVertexInputBindingDescription>
    getBindingDescriptions();
    static std::vector<VkVertexInputAttributeDescription>
    getAttributeDescriptions();

    bool operator==(const Vertex &other) const {
      return position == other.position && color == other.color &&
             normal == other.normal && uv == other.uv;
    }
  };
  void loadModel(const std::string &filepath);

  Model(EngineContext &context, const std::string &name,
        const std::string &filepath);
  Model(EngineContext &context, const std::string &name,
        const std::string &filepath, const std::string &texturepath);
  ~Model();

  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  Model(Model &&other) noexcept;
  Model &operator=(Model &&other) noexcept;

  void bind(VkCommandBuffer commandBuffer);
  void draw(VkCommandBuffer commandBuffer);

  std::string name;

private:
  void createVertexBuffers(const std::vector<Vertex> &vertices);
  void createIndexBuffers(const std::vector<uint32_t> &indices);

  EngineContext &context;

  std::unique_ptr<Buffer> vertexBuffer;
  uint32_t vertexCount;

  bool hasIndexBuffer = false;
  std::unique_ptr<Buffer> indexBuffer;
  uint32_t indexCount;

  bool hasTexture = false;
  VkImage textureImage;
  VkDeviceMemory textureImageMemory;
  VkImageView textureImageView;
  VkSampler textureSampler;
};
} // namespace vkh
