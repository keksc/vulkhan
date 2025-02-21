#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#define STB_TRUETYPE_IMPLEMENTATION
#ifdef WIN32
#include <stb_truetype.h>
#else
#include <stb/stb_truetype.h>
#endif
#include <vulkan/vulkan_core.h>

#include <memory>
#include <stdexcept>
#include <vector>

#include "../buffer.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../image.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"
#include "../swapChain.hpp"

namespace vkh {
namespace fontSys {
std::unique_ptr<GraphicsPipeline> pipeline;
std::unique_ptr<DescriptorSetLayout> descriptorSetLayout;
VkDescriptorSet descriptorSet;
struct Vertex {
  glm::vec2 position{};
  glm::vec2 uv{};
};
std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
    {0, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, position)},
    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}};
std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
    {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};

std::unique_ptr<Buffer> vertexBuffer;
std::unique_ptr<Buffer> indexBuffer;

std::vector<char> fontDataChar;
unsigned char *fontData;

struct Glyph {
  glm::vec2 size;
  glm::vec2 offset;
  glm::vec2 uvOffset;
  glm::vec2 uvExtent;
  float advance;
};

std::vector<Vertex> vertices;
std::vector<uint32_t> indices;

std::unordered_map<char, Glyph> glyphs;
std::shared_ptr<Image> fontAtlas;

const int maxCharCount = 30;
const int maxVertexCount = 4 * maxCharCount; // 4 vertices = 1 quad = 1 glyph
VkDeviceSize maxVertexSize = sizeof(Vertex) * maxVertexCount;
const int maxIndexCount = maxCharCount * 6;
VkDeviceSize maxIndexSize = sizeof(uint32_t) * 6 * maxCharCount;

void createBuffers(EngineContext &context) {
  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = sizeof(Vertex);
  bufInfo.instanceCount = maxVertexCount;
  bufInfo.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);
  vertexBuffer->map();

  bufInfo.instanceSize = sizeof(uint32_t);
  bufInfo.instanceCount = maxIndexCount;
  bufInfo.usage = VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);
  indexBuffer->map();
}
void createDescriptors(EngineContext &context) {
  descriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = fontAtlas->getImageView();
  imageInfo.sampler = context.vulkan.fontSampler;
  DescriptorWriter(*descriptorSetLayout, *context.vulkan.globalDescriptorPool)
      .writeImage(0, &imageInfo)
      .build(descriptorSet);
}
// struct PushConstantData {};

void createPipeline(EngineContext &context) {
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout, *descriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineInfo.attributeDescriptions = attributeDescriptions;
  pipelineInfo.bindingDescriptions = bindingDescriptions;
  pipelineInfo.depthStencilInfo = {
      .sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO,
      .depthTestEnable = false,
      .depthWriteEnable = false,
      .depthCompareOp = VK_COMPARE_OP_LESS};
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/font.vert.spv", "shaders/font.frag.spv",
      pipelineInfo);
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

  ImageCreateInfo imageInfo{};
  imageInfo.format = VK_FORMAT_R8_UNORM;
  imageInfo.w = bitmapExtent.x;
  imageInfo.h = bitmapExtent.y;
  imageInfo.data = atlasData;
  fontAtlas = std::make_shared<Image>(context, imageInfo);

  for (int i = 0; i < charInfo.size(); i++) {
    char c = i + 32;
    const stbtt_packedchar &pc = charInfo[i];

    glm::vec2 size = {
        (pc.xoff2 - pc.xoff) /
            context.vulkan.swapChain->getSwapChainExtent().width,
        (pc.yoff2 - pc.yoff) /
            context.vulkan.swapChain->getSwapChainExtent().height};
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
  createBuffers(context);
  createDescriptors(context);

  createPipeline(context);
}
void updateText(EngineContext &context, std::string text) {
  vertices.clear();
  indices.clear();
  float cursorX = -0.8f; // empirical, TODO: make this be the true left border

  // For each character in the text
  for (size_t i = 0; i < text.length(); i++) {
    char c = text[i];
    auto &ch = glyphs[c];

    // Calculate vertex positions for this character
    float x0 = cursorX;
    float x1 = x0 + ch.size.x;
    float y0 = -1.f;
    float y1 = ch.size.y + y0;

    // Add four vertices for this character
    uint32_t baseIndex = vertices.size();
    vertices.push_back({{x0, y0}, {ch.uvOffset.x, ch.uvOffset.y}});
    vertices.push_back(
        {{x1, y0}, {ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y}});
    vertices.push_back(
        {{x1, y1},

         {ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y + ch.uvExtent.y}});
    vertices.push_back(
        {{x0, y1}, {ch.uvOffset.x, ch.uvOffset.y + ch.uvExtent.y}});

    // Add indices for this character's quad
    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 1);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 0);
    indices.push_back(baseIndex + 2);
    indices.push_back(baseIndex + 3);

    // Move cursor to next character position
    cursorX +=
        ch.advance / context.vulkan.swapChain->getSwapChainExtent().width;
  }
  vertexBuffer->write(vertices.data());
  indexBuffer->write(indices.data());
}

void cleanup(EngineContext &context) {
  vertexBuffer->unmap();
  vertexBuffer = nullptr;
  indexBuffer->unmap();
  indexBuffer = nullptr;
  pipeline = nullptr;
  glyphs.clear();
  fontAtlas = nullptr;
  descriptorSetLayout = nullptr;
}

void render(EngineContext &context) {
  pipeline->bind(context.frameInfo.commandBuffer);

  /*PushConstantData push{};

  vkCmdPushConstants(context.frameInfo.commandBuffer, pipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                     0, sizeof(PushConstantData), &push);*/

  // model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  // model->draw(context.frameInfo.commandBuffer);
  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.commandBuffer, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  VkDescriptorSet descriptorSets[2] = {context.frameInfo.globalDescriptorSet,
                                       descriptorSet};
  vkCmdBindDescriptorSets(context.frameInfo.commandBuffer,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 2,
                          descriptorSets, 0, nullptr);

  vkCmdDrawIndexed(context.frameInfo.commandBuffer, indices.size(), 1, 0, 0, 0);
}
} // namespace fontSys
} // namespace vkh
