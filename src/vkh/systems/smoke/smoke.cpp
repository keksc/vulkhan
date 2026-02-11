#include "smoke.hpp"
#include <GLFW/glfw3.h>

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

#include <cstdlib>
#include <print>

namespace vkh {
void SmokeSys::createBuffer() {
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  // Reserve space for grid quads (6 verts per cell)
  size_t maxVertices = gridSize * 6;

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
  pipelineInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
  pipelineInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "smoke");
}

SmokeSys::SmokeSys(EngineContext &context)
    : System(context), fluidGrid(glm::ivec2{100, 100}, 1.f) {
  createPipeline();
  createBuffer();
}

void SmokeSys::update() {
  static glm::ivec2 prevCursorPos;
  static glm::ivec2 dcp;
  glm::ivec2 cursorPos =
      static_cast<glm::ivec2>(static_cast<glm::vec2>(fluidGrid.cellCount) *
                              (context.input.cursorPos + 1.f) * .5f);
  if (cursorPos - prevCursorPos != glm::ivec2{0})
    dcp = cursorPos - prevCursorPos;
  prevCursorPos = cursorPos;

  if (cursorPos.x >= 1 && cursorPos.x < fluidGrid.cellCount.x - 1 &&
      cursorPos.y >= 1 && cursorPos.y < fluidGrid.cellCount.y - 1) {
    fluidGrid.velX(cursorPos.x, cursorPos.y) = dcp.x * 10.f; // Increased force
    fluidGrid.velY(cursorPos.x, cursorPos.y) = dcp.y * 10.f;
    fluidGrid.smoke(cursorPos.x, cursorPos.y) = 1.0f;
  }

  // Physics Steps
  fluidGrid.solvePressure();
  fluidGrid.updateVelocities();
  fluidGrid.advectVelocities();
  fluidGrid.advectSmoke();

  // Rendering Mesh Generation
  std::vector<Vertex> vertices;
  vertices.reserve(fluidGrid.cellCount.x * fluidGrid.cellCount.y * 6);

  float dx = 2.0f / fluidGrid.cellCount.x;
  float dy = 2.0f / fluidGrid.cellCount.y;

  for (int x = 0; x < fluidGrid.cellCount.x; x++) {
    for (int y = 0; y < fluidGrid.cellCount.y; y++) {

      // Calculate NDC coordinates
      float ndc_x = -1.0f + 2.0f * (float(x) / fluidGrid.cellCount.x);
      float ndc_y = -1.0f + 2.0f * (float(y) / fluidGrid.cellCount.y);

      // Quad vertices
      glm::vec2 a{ndc_x, ndc_y};
      glm::vec2 b{ndc_x + dx, ndc_y};
      glm::vec2 c{ndc_x, ndc_y + dy};
      glm::vec2 d{ndc_x + dx, ndc_y + dy};

      // Color from density
      // Using direct array access for rendering is faster than world-pos
      // interpolation since we are drawing the grid cells directly.
      float density = fluidGrid.smoke(x, y);
      glm::vec3 col = glm::vec3(density);

      // Add 2 triangles
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

// ... (render function remains unchanged) ...
void SmokeSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(
      context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
      &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
      nullptr);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  vkCmdDraw(context.frameInfo.cmd, activeVerticesCount, 1, 0,
            0); // Removed *3 because activeVerticesCount is already total verts
}
} // namespace vkh
