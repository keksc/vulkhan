#include "smoke.hpp"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <memory>

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

namespace vkh {

void SmokeSys::createBuffer() {
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      gridSize * 6);
}
void SmokeSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

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
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo);
}
SmokeSys::SmokeSys(EngineContext &context)
    : System(context), fluidGrid(glm::ivec2{4, 3}, 1.f) {
  createPipeline();
  createBuffer();

  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  grid.reserve(gridSize);
  for (size_t x = 0; x < fluidGrid.cellCount.x; x++) {
    for (size_t y = 0; y < fluidGrid.cellCount.y; y++) {
      size_t idx = x + y * fluidGrid.cellCount.x;
      grid[idx].color = glm::vec3{1.f, static_cast<float>(x) / (gridSize),
                                  static_cast<float>(y) / (gridSize)};
    }
  }
}

void SmokeSys::update() {
  std::vector<Vertex> vertices;
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  vertices.reserve(gridSize);
  for (size_t x = 0; x < fluidGrid.cellCount.x; x++) {
    for (size_t y = 0; y < fluidGrid.cellCount.y; y++) {
      size_t idx = x + y * fluidGrid.cellCount.x;
      float ndc_x = -1.0f + 2.0f * (float(x) / fluidGrid.cellCount.x);
      float ndc_y = -1.0f + 2.0f * (float(y) / fluidGrid.cellCount.x);

      float dx = 2.0f / fluidGrid.cellCount.x; // width of one cell in NDC
      float dy = 2.0f / fluidGrid.cellCount.y;

      glm::vec2 a{ndc_x, ndc_y};
      glm::vec2 b{ndc_x + dx, ndc_y};
      glm::vec2 c{ndc_x, ndc_y + dy};
      glm::vec2 d{ndc_x + dx, ndc_y + dy};

      glm::vec3 col = grid[idx].color;

      // two triangles per quad
      vertices.emplace_back(a, col);
      vertices.emplace_back(b, col);
      vertices.emplace_back(c, col);
      vertices.emplace_back(b, col);
      vertices.emplace_back(c, col);
      vertices.emplace_back(d, col);
    }
  }
  vertexBuffer->map();
  vertexBuffer->write(vertices.data(), vertices.size() * sizeof(Vertex));
  vertexBuffer->unmap();
}

void SmokeSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  vkCmdDraw(context.frameInfo.cmd, gridSize * 6, 1, 0, 0);
}
} // namespace vkh
