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

namespace vkh {
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
  void loadModel(const std::string &filepath);

  Model(EngineContext &context, const std::string &name,
        const std::string &filepath, bool disableTexture = false);
  Model(EngineContext &context, const std::string &name,
        const std::string &filepath, const std::string &texturepath);
  Model(EngineContext &context, const std::string &name,
        std::vector<Vertex> vertices, std::vector<uint32_t> indices);
  Model(EngineContext &context, const std::string &name,
        std::vector<Vertex> vertices, std::vector<uint32_t> indices,
        std::shared_ptr<Image> image);
  ~Model();

  Model(const Model &) = delete;
  Model &operator=(const Model &) = delete;

  void bind(EngineContext &context, VkCommandBuffer commandBuffer,
            VkPipelineLayout pipelineLayout);
  void draw(VkCommandBuffer commandBuffer);

  std::string name;

private:
  void createVertexBuffer(const std::vector<Vertex> &vertices);
  void createIndexBuffer(const std::vector<uint32_t> &indices);
  void createDescriptors();

  EngineContext &context;

  std::unique_ptr<Buffer> vertexBuffer;
  uint32_t vertexCount;

  bool hasIndexBuffer = false;
  std::unique_ptr<Buffer> indexBuffer;
  uint32_t indexCount;

  bool enableTexture = true;

  std::shared_ptr<Image> image;
  VkDescriptorSet textureDescriptorSet;
};
} // namespace vkh
