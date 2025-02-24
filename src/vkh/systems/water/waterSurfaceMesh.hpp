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
#include "skyModel.hpp"

namespace vkh {
/**
 * EXPERIMENTAL
 *  FIXME: incorrect layout when changing to uninitialized frame map pair
 * without animation on
 *  TODO: should be on dedicated thread
 * @brief Enable for double buffered textures: two sets of textures, one used
 * for copying to, the other for rendering to, at each frame they are swapped.
 * For the current use-case, the performance might be slightly worse.
 */
// #define DOUBLE_BUFFERED

/**
 * @brief Represents a water surface rendered as a mesh.
 *
 */
namespace waterSys {
static const uint32_t s_kMinTileSize{16};
static const uint32_t maxTileSize{1024};

struct Vertex {
  glm::vec3 pos;
  glm::vec2 uv;
  glm::vec3 normal;

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
// =========================================================================
/**
 * @brief Creates vertex and index buffers to accomodate maximum size of
 *  vertices and indices.
 *  To render the mesh, fnc "Prepare()" must be called with the size of tile
 */
void init(EngineContext &context);
void cleanup();
void update(EngineContext &context, float dt);
void render(EngineContext &context, const uint32_t frameIndex,
            VkCommandBuffer cmdBuffer);
void prepare(EngineContext &context, VkCommandBuffer cmdBuffer);
void prepareRender(EngineContext &context, const uint32_t frameIndex,
                   VkCommandBuffer cmdBuffer, const glm::mat4 &viewMat,
                   const glm::mat4 &projMat, const glm::vec3 &camPos,
                   const skySys::Params &skyParams);

} // namespace waterSys

} // namespace vkh
