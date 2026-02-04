#include "text.hpp"

#include <vulkan/vulkan.h>
#include <vulkan/vulkan_core.h>
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
#include "../../swapChain.hpp"

namespace vkh {
TextSys::GlyphRange TextSys::glyphRange;
TextSys::~TextSys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, setLayout, nullptr);
}
void TextSys::createBuffers() {
  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxVertexCount);
  vertexBuffer->map();

  indexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxIndexCount);
  indexBuffer->map();
}
void TextSys::createDescriptors() {
  setLayout = buildDescriptorSetLayout(
      context, {VkDescriptorSetLayoutBinding{
                   .binding = 0,
                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   .descriptorCount = 1,
                   .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
               }});
  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
  DescriptorWriter writer(context);
  writer.writeImage(0,
                    fontAtlas->getDescriptorInfo(context.vulkan.defaultSampler),
                    VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER);
  writer.updateSet(set);
}
void TextSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.vertpath = "shaders/text.vert.spv";
  pipelineInfo.fragpath = "shaders/text.frag.spv";
  pipelineInfo.depthStencilInfo.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "text");
}
void TextSys::createGlyphs() {
  std::filesystem::path fontPath = "fonts/Roboto-Regular.ttf";
  std::vector<char> fontDataChar = readFile(fontPath);
  unsigned char *fontData =
      reinterpret_cast<unsigned char *>(fontDataChar.data());
  const glm::uvec2 bitmapExtent{512, 512};
  const unsigned int atlasSize = bitmapExtent.x * bitmapExtent.y;
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
                           static_cast<int>(charInfo.size()),
                           charInfo.data())) {
    stbtt_PackEnd(&packContext);
    delete[] atlasData;
    throw std::runtime_error("Failed to pack font");
  }
  stbtt_PackEnd(&packContext);

  ImageCreateInfo imageInfo{};
  imageInfo.format = VK_FORMAT_R8_UNORM;
  imageInfo.size = bitmapExtent;
  imageInfo.data = atlasData;
  std::string imageName = std::format("font atlas for {}", fontPath.string());
  imageInfo.name = imageName.c_str();
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
                .uvOffset = glm::vec2(pc.x0, pc.y0) /
                            static_cast<glm::vec2>(bitmapExtent),
                .uvExtent = glm::vec2(pc.x1 - pc.x0, pc.y1 - pc.y0) /
                            static_cast<glm::vec2>(bitmapExtent),
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
  debug::beginLabel(context, context.frameInfo.cmd, "TextSys rendering",
                    glm::vec4{.2f, .7f, .6f, 1.f});
  pipeline->bind(context.frameInfo.cmd);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  vkCmdBindIndexBuffer(context.frameInfo.cmd, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  VkDescriptorSet descriptorSets[2] = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], set};
  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 2,
                          descriptorSets, 0, nullptr);

  vkCmdDrawIndexed(context.frameInfo.cmd, static_cast<uint32_t>(indicesSize), 1,
                   0, 0, 0);
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
