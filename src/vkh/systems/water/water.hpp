#pragma once

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <array>
#include <memory>
#include <vector>

#include "../../engineContext.hpp"
#include "../../image.hpp"
#include "../../mesh.hpp"
#include "WSTessendorf.hpp"
#include "skyPreetham.hpp"

namespace vkh {
namespace waterSys {

struct SkyParams {
  alignas(16) glm::vec3 sunColor{1.0};
  float sunIntensity{1.0};
  // -------------------------------------------------
  SkyPreetham::Props props;
};
/**
 * @brief Creates vertex and index buffers to accomodate maximum size of
 *  vertices and indices.
 *  To render the mesh, fnc "Prepare()" must be called with the size of tile
 */
void init(EngineContext &context);
void cleanup();
void createRenderData(EngineContext &context, const uint32_t imageCount);
void prepare(EngineContext &context, VkCommandBuffer cmdBuffer);
void update(EngineContext &context);
void prepareRender(EngineContext &context,
                   const SkyParams &skyParams);
void render(EngineContext &context);

struct Vertex {
  glm::vec3 pos;
  glm::vec2 uv;

  Vertex(const glm::vec3 &position, const glm::vec2 &texCoord)
      : pos(position), uv(texCoord) {}

  constexpr static VkVertexInputBindingDescription GetBindingDescription() {
    return VkVertexInputBindingDescription{.binding = 0,
                                           .stride = sizeof(Vertex),
                                           .inputRate =
                                               VK_VERTEX_INPUT_RATE_VERTEX};
  }

  static std::vector<VkVertexInputAttributeDescription>
  GetAttributeDescriptions() {
    return std::vector<VkVertexInputAttributeDescription>{
        {.location = 0,
         .binding = 0,
         .format = VK_FORMAT_R32G32B32_SFLOAT,
         .offset = offsetof(Vertex, pos)},
        {.location = 1,
         .binding = 0,
         .format = VK_FORMAT_R32G32_SFLOAT,
         .offset = offsetof(Vertex, uv)}};
  }

  static const inline std::vector<VkVertexInputBindingDescription>
      s_BindingDescriptions{GetBindingDescription()};

  static const inline std::vector<VkVertexInputAttributeDescription>
      s_AttribDescriptions{GetAttributeDescriptions()};
};
static constexpr VkFormat mapFormat = VK_FORMAT_R32G32B32A32_SFLOAT;
static constexpr bool useMipMapping = false;
struct FrameMapData {
  std::unique_ptr<Image> displacementMap{nullptr};
  std::unique_ptr<Image> normalMap{nullptr};
};
} // namespace waterSys
} // namespace vkh
