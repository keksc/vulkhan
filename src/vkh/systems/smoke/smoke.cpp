#include "smoke.hpp"

#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <vulkan/vulkan_core.h>

#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

#include <cstdlib>
#include <print>
#include <random>

namespace vkh {
void SmokeSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, fluidGrid.updateSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.vertpath = "shaders/smoke/smoke.vert.spv";
  pipelineInfo.fragpath = "shaders/smoke/smoke.frag.spv";
  pipelineInfo.depthStencilInfo.depthTestEnable = VK_TRUE;
  pipelineInfo.depthStencilInfo.depthWriteEnable = VK_TRUE;
  pipelineInfo.subpass = 1;
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "smoke");
}

SmokeSys::SmokeSys(EngineContext &context)
    : System(context), fluidGrid(context, glm::uvec2{600, 600}, 1.f) {
  stagingBuffer =
      std::make_unique<Buffer<float>>(context, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                                      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                                          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                      fluidGrid.smokeMap.size());
  stagingBuffer->map();
  createPipeline();
}

std::mt19937 rng{std::random_device{}()};
std::uniform_int_distribution<> velocity(-10, 10);
void SmokeSys::update() {
  static glm::ivec2 prevCursorPos;
  static glm::ivec2 dcp;
  glm::ivec2 cursorPos =
      static_cast<glm::ivec2>(static_cast<glm::vec2>(fluidGrid.cellCount) *
                              (context.input.cursorPos + 1.f) * .5f);
  if (cursorPos - prevCursorPos != glm::ivec2{0})
    dcp = cursorPos - prevCursorPos;
  prevCursorPos = cursorPos;

  float brushRadius = 12.0f;
  float brushRadiusSq = brushRadius * brushRadius;
  float strength = 15.f;

  if (glfwGetMouseButton(context.window, GLFW_MOUSE_BUTTON_LEFT) ==
      GLFW_PRESS) {
    for (int dy = -brushRadius; dy <= brushRadius; dy++) {
      for (int dx = -brushRadius; dx <= brushRadius; dx++) {
        float distSq = (float)(dx * dx + dy * dy);
        if (distSq > brushRadiusSq)
          continue;

        float dist = sqrt(dx * dx + dy * dy);

        int x = cursorPos.x + dx;
        int y = cursorPos.y + dy;

        if (x <= 0 || x >= fluidGrid.cellCount.x - 1 || y <= 0 ||
            y >= fluidGrid.cellCount.y - 1)
          continue;

        float falloff = 1.0f - (dist / brushRadius);

        fluidGrid.velX(x, y) += dcp.x * strength * falloff;
        fluidGrid.velY(x, y) += dcp.y * strength * falloff;
        fluidGrid.smoke(x, y) = std::min(1.0f, fluidGrid.smoke(x, y) + falloff);
      }
    }
  }

  fluidGrid.update();

  memcpy(stagingBuffer->getMappedAddr(), fluidGrid.smokeMap.data(),
         fluidGrid.smokeMap.size() * sizeof(float));
  fluidGrid.dyeImage->copyFromBuffer(*stagingBuffer);
}

void SmokeSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  VkDescriptorSet sets[] = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      fluidGrid.dyeImageSet};
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 2,
                          sets, 0, nullptr);

  vkCmdDraw(context.frameInfo.cmd, 3, 1, 0, 0);
}
} // namespace vkh
