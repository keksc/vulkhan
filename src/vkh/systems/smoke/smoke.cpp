#include "smoke.hpp"
#include <GLFW/glfw3.h>
#include <cstdlib>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {

void SmokeSys::createBuffer() {
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  unsigned int numArrows = (fluidGrid.cellCount.x * interpolatedScale) *
                           (fluidGrid.cellCount.y * interpolatedScale);

  size_t maxVertices = (gridSize * 6) + (numArrows * 3);

  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxVertices);
}
void SmokeSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.vertpath = "shaders/smoke.vert.spv";
  pipelineInfo.fragpath = "shaders/smoke.frag.spv";
  pipelineInfo.depthStencilInfo.depthTestEnable = VK_TRUE,
  pipelineInfo.depthStencilInfo.depthWriteEnable = VK_TRUE,
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "smoke");
}
SmokeSys::SmokeSys(EngineContext &context)
    : System(context), fluidGrid(glm::ivec2{15, 15}, 1.f) {
  createPipeline();
  createBuffer();
}

void SmokeSys::update() {
  std::vector<Vertex> vertices;
  const float arrowScale = .2f;
  fluidGrid.advectVelocities();
  fluidGrid.solvePressure();
  fluidGrid.updateVelocities();
  for (size_t x = 0; x < fluidGrid.cellCount.x * interpolatedScale; x++) {
    for (size_t y = 0; y < fluidGrid.cellCount.y * interpolatedScale; y++) {
      float fx = (static_cast<float>(x) + 0.5f) /
                 (fluidGrid.cellCount.x * interpolatedScale);
      float fy = (static_cast<float>(y) + 0.5f) /
                 (fluidGrid.cellCount.y * interpolatedScale);

      // Map to NDC range (-1.0 to 1.0)
      glm::vec2 ndc{-1.f + 2.f * fx, -1.f + 2.f * fy};

      glm::vec2 worldPos{fx * fluidGrid.cellCount.x,
                         fy * fluidGrid.cellCount.y};

      glm::vec2 velocity = fluidGrid.getVelocityAtWorldPos(worldPos);

      float speed = glm::length(velocity);
      if (speed > 0.0001f) {
        glm::vec2 dir = velocity / speed;
        glm::vec2 side =
            glm::vec2(-dir.y, dir.x) * 0.3f; // Perpendicular for width

        // Base of the arrow (centered at the sample point)
        glm::vec2 base = ndc;
        // Tip of the arrow (scaled by velocity)
        glm::vec2 tip = ndc + velocity * arrowScale;
        // Left/Right wings for a triangle shape
        glm::vec2 left =
            ndc + (velocity * arrowScale * 0.7f) + (side * arrowScale);
        glm::vec2 right =
            ndc + (velocity * arrowScale * 0.7f) - (side * arrowScale);

        glm::vec3 col =
            glm::mix(glm::vec3(0.2, 0.2, 1.0), glm::vec3(1.0, 0.2, 0.2), speed);

        vertices.emplace_back(tip, col);
        vertices.emplace_back(left, col);
        vertices.emplace_back(right, col);
      }
    }
  }
  for (size_t x = 0; x < fluidGrid.cellCount.x; x++) {
    for (size_t y = 0; y < fluidGrid.cellCount.y; y++) {
      size_t idx = x + y * fluidGrid.cellCount.x;
      float ndc_x = -1.0f + 2.0f * (float(x) / fluidGrid.cellCount.x);
      float ndc_y = -1.0f + 2.0f * (float(y) / fluidGrid.cellCount.y);

      float dx = 2.0f / fluidGrid.cellCount.x;
      float dy = 2.0f / fluidGrid.cellCount.y;

      glm::vec2 a{ndc_x, ndc_y};
      glm::vec2 b{ndc_x + dx, ndc_y};
      glm::vec2 c{ndc_x, ndc_y + dy};
      glm::vec2 d{ndc_x + dx, ndc_y + dy};

      const float divergenceDisplayRange = 2.f;
      float divergence =
          fluidGrid.calculateVelocityDivergence(glm::ivec2{x, y});
      // float divergence = fluidGrid.pressureMap[x + y *
      // fluidGrid.cellCount.x];
      float divergenceT = glm::abs(divergence) / divergenceDisplayRange;
      glm::vec3 col{0.f};
      glm::vec3 negativeDivergenceCol{1.f, 0.f, 0.f};
      glm::vec3 positiveDivergenceCol{0.f, 0.f, 1.f};
      col = glm::mix(
          col, divergence < 0 ? negativeDivergenceCol : positiveDivergenceCol,
          divergenceT);

      vertices.emplace_back(a, col);
      vertices.emplace_back(b, col);
      vertices.emplace_back(c, col);
      vertices.emplace_back(b, col);
      vertices.emplace_back(c, col);
      vertices.emplace_back(d, col);
    }
  }

  activeVerticesCount = vertices.size();
  vertexBuffer->map();
  vertexBuffer->write(vertices.data(), vertices.size() * sizeof(Vertex));
  vertexBuffer->unmap();
}

void SmokeSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(
      context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
      &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
      nullptr);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  vkCmdDraw(context.frameInfo.cmd, activeVerticesCount * 3, 1, 0, 0);
}
} // namespace vkh
