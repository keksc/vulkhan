#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

#include "debug.hpp"
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
        const std::string &filepath, bool disableTexture = false);
  Model(EngineContext &context, const std::string &name,
        const std::string &filepath, const std::string &texturepath);
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
  void createDefaultBlackTextureImage();
  inline void setDebugObjNames() {
    debug::setObjName(context, fmt::format("{} image", name),
                      VK_OBJECT_TYPE_IMAGE, textureImage);
    debug::setObjName(context, fmt::format("{} image view", name),
                      VK_OBJECT_TYPE_IMAGE_VIEW, textureImageView);
    debug::setObjName(context, fmt::format("{} sampler", name),
                      VK_OBJECT_TYPE_SAMPLER, textureSampler);
    debug::setObjName(context, fmt::format("{} image memory", name),
                      VK_OBJECT_TYPE_DEVICE_MEMORY, textureImageMemory);
  };

  EngineContext &context;

  std::unique_ptr<Buffer> vertexBuffer;
  uint32_t vertexCount;

  bool hasIndexBuffer = false;
  std::unique_ptr<Buffer> indexBuffer;
  uint32_t indexCount;

  bool enableTexture = true;

  VkImage textureImage;
  VkDeviceMemory textureImageMemory;
  VkImageView textureImageView;
  VkSampler textureSampler; // TODO: 1 global texture sampler should be enough
                            // for the whole program, not 1 per model
  VkDescriptorSet textureDescriptorSet;
};
} // namespace vkh
