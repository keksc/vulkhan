#include "text.hpp"

#include <vulkan/vulkan.hpp>
#define STB_TRUETYPE_IMPLEMENTATION
#ifdef WIN32
#include <stb_truetype.h>
#else
#include <stb/stb_truetype.h>
#endif

#include <cstring>
#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {

TextSys::GlyphRange TextSys::glyphRange;

TextSys::~TextSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
  }
}

void TextSys::createBuffers() {
  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      maxVertexCount);
  vertexBuffer->map();

  indexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, vk::BufferUsageFlagBits::eIndexBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      maxIndexCount);
  indexBuffer->map();
}

void TextSys::ensureCapacity(size_t vertexCount, size_t indexCount) {
  bool needsRecreation = false;
  if (vertexCount > maxVertexCount) {
    maxVertexCount =
        std::max(static_cast<int>(vertexCount), maxVertexCount * 2);
    needsRecreation = true;
  }
  if (indexCount > maxIndexCount) {
    maxIndexCount = std::max(static_cast<int>(indexCount), maxIndexCount * 2);
    needsRecreation = true;
  }

  if (needsRecreation) {
    context.vulkan.device.waitIdle();
    vertexBuffer = std::make_unique<Buffer<Vertex>>(
        context, vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxVertexCount);
    vertexBuffer->map();

    indexBuffer = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxIndexCount);
    indexBuffer->map();
  }
}

void TextSys::createDescriptors() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr}};

  setLayout = buildDescriptorSetLayout(context, bindings);
  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

  DescriptorWriter writer(context);
  writer.writeImage(0,
                    fontAtlas->getDescriptorInfo(context.vulkan.defaultSampler),
                    vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(set);
}

void TextSys::createPipeline() {
  std::array<vk::DescriptorSetLayout, 2> setLayouts{
      context.vulkan.globalDescriptorSetLayout, setLayout};

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
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
  pipelineInfo.depthStencilInfo.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
  pipelineInfo.subpass = 0;
  pipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "text");
}

void TextSys::createGlyphs() {
  std::filesystem::path fontPath = "fonts/EBGaramond-Regular.ttf";
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

  ImageCreateInfo_data imageInfo{};
  imageInfo.format = vk::Format::eR8Unorm;
  imageInfo.size = bitmapExtent;
  imageInfo.data = atlasData;
  std::string imageName = std::format("font atlas for {}", fontPath.string());
  imageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
  imageInfo.name = imageName.c_str();
  fontAtlas = std::make_unique<Image>(context, imageInfo);

  for (int i = 0; i < charInfo.size(); i++) {
    char c = i + 32;
    const stbtt_packedchar &pc = charInfo[i];

    glm::vec2 size = glm::vec2{pc.xoff2 - pc.xoff, pc.yoff2 - pc.yoff} /
                     static_cast<glm::vec2>(context.window.size) * 2.f;
    glm::vec2 offset = glm::vec2{pc.xoff, pc.yoff} /
                       static_cast<glm::vec2>(context.window.size) * 2.f;
    float advance = pc.xadvance / context.window.size.x * 2.f;
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
  auto cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "TextSys rendering",
                    glm::vec4{.2f, .7f, .6f, 1.f});
  pipeline->bind(cmd);

  vk::Buffer buffers[] = {*vertexBuffer};
  vk::DeviceSize offsets[] = {0};
  cmd.bindVertexBuffers(0, 1, buffers, offsets);
  cmd.bindIndexBuffer(*indexBuffer, 0, vk::IndexType::eUint32);

  std::array<vk::DescriptorSet, 2> descriptorSets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], set};
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *pipeline, 0,
                         static_cast<uint32_t>(descriptorSets.size()),
                         descriptorSets.data(), 0, nullptr);

  cmd.drawIndexed(static_cast<uint32_t>(indicesSize), 1, 0, 0, 0);
  debug::endLabel(context, cmd);
}

} // namespace vkh
