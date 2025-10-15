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

#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {

void SmokeSys::createBuffer() {
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  unsigned int arrowsSize =
      (fluidGrid.cellCount.x + 1) * fluidGrid.cellCount.y +
      fluidGrid.cellCount.x * (fluidGrid.cellCount.y + 1);
  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      gridSize * 6 + arrowsSize * 3);
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
    : System(context), fluidGrid(glm::ivec2{5, 3}, 1.f) {
  createPipeline();
  createBuffer();
}

void SmokeSys::update() {
  std::vector<Vertex> vertices;
  unsigned int gridSize = fluidGrid.cellCount.x * fluidGrid.cellCount.y;
  unsigned int arrowsSize =
      (fluidGrid.cellCount.x + 1) * fluidGrid.cellCount.y +
      fluidGrid.cellCount.x * (fluidGrid.cellCount.y + 1);
  vertices.reserve(gridSize * 6 + arrowsSize * 3);
  const float arrowScale = .2f;
  for (size_t x = 0; x < fluidGrid.cellCount.x + 1; x++) {
    for (size_t y = 0; y < fluidGrid.cellCount.y; y++) {
      float ndc_x = -1.0f + 2.0f * (float(x) / fluidGrid.cellCount.x);
      float ndc_y = -1.0f + 2.0f * (float(y) / fluidGrid.cellCount.y);

      float dx = 2.0f / fluidGrid.cellCount.x;
      float dy = 2.0f / fluidGrid.cellCount.y;

      glm::vec2 a{ndc_x, ndc_y + dy * arrowScale};
      glm::vec2 b{ndc_x, ndc_y + dy * (1.f - arrowScale)};
      glm::vec2 c{ndc_x + fluidGrid.velX(x, y) * arrowScale, ndc_y + dy * .5f};

      glm::vec3 col{1.f};

      vertices.emplace_back(a, col);
      vertices.emplace_back(b, col);
      vertices.emplace_back(c, col);
    }
  }
  for (size_t x = 0; x < fluidGrid.cellCount.x; x++) {
    for (size_t y = 0; y < fluidGrid.cellCount.y + 1; y++) {
      float ndc_x = -1.0f + 2.0f * (float(x) / fluidGrid.cellCount.x);
      float ndc_y = -1.0f + 2.0f * (float(y) / fluidGrid.cellCount.y);

      float dx = 2.0f / fluidGrid.cellCount.x;
      float dy = 2.0f / fluidGrid.cellCount.y;

      glm::vec2 a{ndc_x + dx * arrowScale, ndc_y};
      glm::vec2 b{ndc_x + dx * (1.f - arrowScale), ndc_y};
      glm::vec2 c{ndc_x + dx * .5f, ndc_y + fluidGrid.velY(x, y) * arrowScale};

      glm::vec3 col{1.f};

      vertices.emplace_back(a, col);
      vertices.emplace_back(b, col);
      vertices.emplace_back(c, col);
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
  unsigned int arrowsSize =
      (fluidGrid.cellCount.x + 1) * fluidGrid.cellCount.y +
      fluidGrid.cellCount.x * (fluidGrid.cellCount.y + 1);
  vkCmdDraw(context.frameInfo.cmd, gridSize * 6 + arrowsSize * 3, 1, 0, 0);
}
} // namespace vkh
