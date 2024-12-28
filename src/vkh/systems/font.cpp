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
#include "../model.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace fontSys {
std::unique_ptr<Pipeline> pipeline;
VkPipelineLayout pipelineLayout;

std::unique_ptr<Model> model;

std::vector<char> fontDataChar;
unsigned char *fontData;

struct Glyph {
  glm::vec2 size;
  glm::vec2 offset;
  glm::vec2 uvOffset;
  glm::vec2 uvExtent;
  float advance;
};

std::string text = "abcd";

std::unordered_map<char, Glyph> glyphs;
std::shared_ptr<Image> fontAtlas;

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
  fontDataChar = readFile("fonts/Roboto-Regular.ttf");
  fontData = reinterpret_cast<unsigned char *>(fontDataChar.data());
}
void createGlyphs(EngineContext &context) {
  const glm::vec2 bitmapExtent = {512.f, 512.f};
  const int atlasSize = bitmapExtent.x * bitmapExtent.y;
  const float fontSize = 32.f;

  unsigned char *atlasData = new unsigned char[atlasSize];
  std::memset(atlasData, 0, atlasSize);

  stbtt_pack_context packContext;
  if (!stbtt_PackBegin(&packContext, atlasData, bitmapExtent.x, bitmapExtent.y,
                       0, 1, nullptr)) {
    throw std::runtime_error("Failed to initialize font packing");
  }
  std::vector<stbtt_packedchar> charInfo(127 - 32);
  stbtt_PackSetOversampling(&packContext, 1, 1);
  if (!stbtt_PackFontRange(&packContext, fontData, 0, fontSize, 32,
                           charInfo.size(), charInfo.data())) {
    stbtt_PackEnd(&packContext);
    delete[] atlasData;
    throw std::runtime_error("Failed to pack font");
  }
  stbtt_PackEnd(&packContext);

  fontAtlas =
      std::make_shared<Image>(context, "font atlas", bitmapExtent.x,
                              bitmapExtent.y, atlasData, VK_FORMAT_R8_UNORM);

  for (int i = 0; i < charInfo.size(); i++) {
    char c = i + 32;
    const stbtt_packedchar &pc = charInfo[i];

    glm::vec2 size = {(pc.xoff2 - pc.xoff)/context.vulkan.swapChain->getSwapChainExtent().width, (pc.yoff2 - pc.yoff)/context.vulkan.swapChain->getSwapChainExtent().height};
    glm::vec2 offset = {pc.xoff, pc.yoff};
    Glyph glyph{.size = size,
                .offset = offset,
                .uvOffset = glm::vec2(pc.x0, pc.y0) / bitmapExtent,
                .uvExtent =
                    glm::vec2(pc.x1 - pc.x0, pc.y1 - pc.y0) / bitmapExtent,
                .advance = pc.xadvance};

    glyphs[c] = glyph;
  }
  delete[] atlasData;
}
void init(EngineContext &context) {
  initFont();
  createGlyphs(context);

  glm::vec3 col(0.0);
  glm::vec3 n(0.0);
  auto ch = glyphs['b'];
  float scale = 0.01f; // Adjust this value as needed

  std::vector<Model::Vertex> vertices = {
      {{0.f, 0.f, 0.0}, col, n, {ch.uvOffset.x, ch.uvOffset.y}},
      {{ch.size.x, 0.f, 0.0},
       col,
       n,
       {ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y}},
      {{ch.size.x, ch.size.y, 0.0},
       col,
       n,
       {ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y + ch.uvExtent.y}},
      {{0.f, ch.size.y, 0.0},
       col,
       n,
       {ch.uvOffset.x, ch.uvOffset.y + ch.uvExtent.y}},
  };
  std::vector<uint32_t> indices = {0, 1, 2, 0, 3, 2};

  model = std::make_unique<Model>(context, "font atlas", vertices, indices,
                                  fontAtlas);

  createPipelineLayout(context);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  pipeline = nullptr;
  glyphs.clear();
  fontAtlas = nullptr;
  model = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, pipelineLayout, nullptr);
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  /*PushConstantData push{};

  vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PushConstantData), &push);*/

  model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  model->draw(context.frameInfo.commandBuffer);
}
} // namespace fontSys
} // namespace vkh
