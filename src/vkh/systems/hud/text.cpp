#include "text.hpp"

#include <vulkan/vulkan.h>
#define STB_TRUETYPE_IMPLEMENTATION
#ifdef WIN32
#include <stb_truetype.h>
#else
#include <stb/stb_truetype.h>
#endif

#include <memory>
#include <vector>

#include <cstring>

#include "../../buffer.hpp"
#include "../../descriptors.hpp"
#include "../../pipeline.hpp"

namespace vkh {
TextSys::GlyphRange TextSys::glyphRange;

void TextSys::createBuffers() {
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
void TextSys::createDescriptors() {
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
void TextSys::createPipeline() {
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
      context, "shaders/text.vert.spv", "shaders/text.frag.spv", pipelineInfo);
}
void TextSys::createGlyphs() {
  std::vector<char> fontDataChar = readFile("fonts/Roboto-Regular.ttf");
  unsigned char *fontData =
      reinterpret_cast<unsigned char *>(fontDataChar.data());
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
  fontAtlas = std::make_unique<Image>(context, imageInfo);

  for (int i = 0; i < charInfo.size(); i++) {
    char c = i + 32;
    const stbtt_packedchar &pc = charInfo[i];

    glm::vec2 size = glm::vec2{pc.xoff2 - pc.xoff, pc.yoff2 - pc.yoff} /
                     static_cast<glm::vec2>(context.window.size);
    glm::vec2 offset = glm::vec2{pc.xoff, pc.yoff} /
                       static_cast<glm::vec2>(context.window.size);
    float advance = pc.xadvance / context.window.size.x;
    Glyph glyph{.size = size,
                .offset = offset,
                .uvOffset = glm::vec2(pc.x0, pc.y0) / bitmapExtent,
                .uvExtent =
                    glm::vec2(pc.x1 - pc.x0, pc.y1 - pc.y0) / bitmapExtent,
                .advance = advance};

    glyphRange.glyphs[c] = glyph;
    glyphRange.maxSizeY = glm::max(size.y, glyphRange.maxSizeY);
  }
  delete[] atlasData;
}
TextSys::TextSys(EngineContext &context) : System(context) {
  createGlyphs();
  createBuffers();
  createDescriptors();

  createPipeline();
}
void TextSys::render(size_t indicesSize) {
  pipeline->bind(context.frameInfo.commandBuffer);

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

  vkCmdDrawIndexed(context.frameInfo.commandBuffer, indicesSize, 1, 0, 0, 0);
}
} // namespace vkh
