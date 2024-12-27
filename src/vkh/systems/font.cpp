#include "entities.hpp"
#include <memory>
#include <utility>
#include <vulkan/vulkan_core.h>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#define STB_TRUETYPE_IMPLEMENTATION
#include <stb/stb_truetype.h>

#include <stdexcept>
#include <vector>

#include <cassert>

#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../entity.hpp"
#include "../model.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace fontSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

stbtt_fontinfo fontInfo;
std::unique_ptr<Model> model;
std::vector<std::shared_ptr<Image>> glyphs;

// struct PushConstantData {};

void createPipelineLayout(EngineContext &context) {
  /*VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags =
      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);*/

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      context.vulkan.globalDescriptorSetLayout->getDescriptorSetLayout(),
      context.vulkan.modelDescriptorSetLayout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  // pipelineLayoutInfo.pushConstantRangeCount = 1;
  // pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &pipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");
}
void createPipeline(EngineContext &context) {
  assert(pipelineLayout != nullptr &&
         "Cannot create pipeline before pipeline layout");

  PipelineConfigInfo pipelineConfig{};
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.pipelineLayout = pipelineLayout;
  pipelineConfig.depthStencilInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = false,
      .depthWriteEnable = false,
      .depthCompareOp = VK_COMPARE_OP_LESS};
  Pipeline::enableAlphaBlending(pipelineConfig);
  pipeline = std::make_unique<Pipeline>(
      context, "entity system", "shaders/font.vert.spv",
      "shaders/font.frag.spv", pipelineConfig);
}
void initFont() {
  std::vector<char> fontDataChar = readFile("fonts/Roboto-Regular.ttf");
  auto fontData = reinterpret_cast<unsigned char *>(fontDataChar.data());
  stbtt_InitFont(&fontInfo, fontData, stbtt_GetFontOffsetForIndex(fontData, 0));
}
void createGlyphImages(EngineContext &context) {
  char c = 'a';
  int w, h, xoff, yoff;
  unsigned char *bitmap = stbtt_GetGlyphBitmap(
      &fontInfo, 1.0, 1.0, stbtt_FindGlyphIndex(&fontInfo, c), &w, &h, &xoff,
      &yoff);
  VkDeviceSize imageSize = w * h;
  glyphs.push_back(std::make_shared<Image>(
      context, fmt::format("glyph {}", static_cast<char>(c)), w, h, bitmap,
      VK_FORMAT_R8_UNORM));
  stbtt_FreeBitmap(bitmap, nullptr);
}
inline int getCharIndexInGlyphs(char c) { return c + 32; }
void init(EngineContext &context) {
  initFont();
  createGlyphImages(context);

  glm::vec3 col(0.0);
  glm::vec3 n(0.0);
  std::vector<Model::Vertex> vertices = {
      {{-1.0, -1.0, 0.0}, col, n, {-1.0, -1.0}},
      {{1.0, -1.0, 0.0}, col, n, {1.0, -1.0}},
      {{1.0, 1.0, 0.0}, col, n, {1.0, 1.0}},
      {{-1.0, 1.0, 0.0}, col, n, {-1.0, 1.0}},
  };
  std::vector<uint32_t> indices = {0, 1, 2, 0, 3, 2};
  model = std::make_unique<Model>(context, "font model", vertices, indices,
                                  glyphs[0]);

  createPipelineLayout(context);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
  glyphs.clear();
  model = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  /*vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout, 0, 1,
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);*/

  /*PushConstantData push{};

  vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PushConstantData), &push);*/
  model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  model->draw(context.frameInfo.commandBuffer);
}
} // namespace fontSys
} // namespace vkh
