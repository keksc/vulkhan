#include "solidColor.hpp"

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {

void SolidColorSys::createBuffers() {
  linesVertexBuffer = std::make_unique<Buffer<LineVertex>>(
      context, vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      maxLineVertexCount);
  linesVertexBuffer->map();

  linesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, vk::BufferUsageFlagBits::eIndexBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      maxLineIndexCount);
  linesIndexBuffer->map();

  trianglesVertexBuffer = std::make_unique<Buffer<TriangleVertex>>(
      context, vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      maxTriangleVertexCount);
  trianglesVertexBuffer->map();

  trianglesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, vk::BufferUsageFlagBits::eIndexBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      maxTriangleIndexCount);
  trianglesIndexBuffer->map();
}

void SolidColorSys::ensureLineCapacity(size_t vertexCount, size_t indexCount) {
  bool needsRecreation = false;
  if (vertexCount > maxLineVertexCount) {
    maxLineVertexCount =
        std::max(static_cast<int>(vertexCount), maxLineVertexCount * 2);
    needsRecreation = true;
  }
  if (indexCount > maxLineIndexCount) {
    maxLineIndexCount =
        std::max(static_cast<int>(indexCount), maxLineIndexCount * 2);
    needsRecreation = true;
  }

  if (needsRecreation) {
    context.vulkan.device.waitIdle();
    linesVertexBuffer = std::make_unique<Buffer<LineVertex>>(
        context, vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxLineVertexCount);
    linesVertexBuffer->map();

    linesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxLineIndexCount);
    linesIndexBuffer->map();
  }
}

void SolidColorSys::ensureTriangleCapacity(size_t vertexCount,
                                           size_t indexCount) {
  bool needsRecreation = false;
  if (vertexCount > maxTriangleVertexCount) {
    maxTriangleVertexCount =
        std::max(static_cast<int>(vertexCount), maxTriangleVertexCount * 2);
    needsRecreation = true;
  }
  if (indexCount > maxTriangleIndexCount) {
    maxTriangleIndexCount =
        std::max(static_cast<int>(indexCount), maxTriangleIndexCount * 2);
    needsRecreation = true;
  }

  if (needsRecreation) {
    context.vulkan.device.waitIdle();
    trianglesVertexBuffer = std::make_unique<Buffer<TriangleVertex>>(
        context, vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxTriangleVertexCount);
    trianglesVertexBuffer->map();

    trianglesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eIndexBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxTriangleIndexCount);
    trianglesIndexBuffer->map();
  }
}

void SolidColorSys::createPipelines() {
  std::array<vk::DescriptorSetLayout, 2> setLayouts{
      context.vulkan.globalDescriptorSetLayout, texturesSetLayout};

  PipelineCreateInfo trianglesPipelineInfo{};
  trianglesPipelineInfo.layoutInfo.setLayoutCount =
      static_cast<uint32_t>(setLayouts.size());
  trianglesPipelineInfo.layoutInfo.pSetLayouts = setLayouts.data();
  trianglesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  trianglesPipelineInfo.attributeDescriptions =
      TriangleVertex::getAttributeDescriptions();
  trianglesPipelineInfo.bindingDescriptions =
      TriangleVertex::getBindingDescriptions();
  trianglesPipelineInfo.vertpath = "shaders/solidColorTriangles.vert.spv";
  trianglesPipelineInfo.fragpath = "shaders/solidColorTriangles.frag.spv";
  trianglesPipelineInfo.depthStencilInfo.depthCompareOp =
      vk::CompareOp::eGreaterOrEqual;
  trianglesPipelineInfo.subpass = 0;
  trianglesPipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);
  GraphicsPipeline::enableAlphaBlending(trianglesPipelineInfo);
  trianglePipeline = std::make_unique<GraphicsPipeline>(
      context, trianglesPipelineInfo, "solid color triangles");

  PipelineCreateInfo linesPipelineInfo{};
  linesPipelineInfo.layoutInfo.setLayoutCount =
      static_cast<uint32_t>(setLayouts.size());
  linesPipelineInfo.layoutInfo.pSetLayouts = setLayouts.data();
  linesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  linesPipelineInfo.attributeDescriptions =
      LineVertex::getAttributeDescriptions();
  linesPipelineInfo.bindingDescriptions = LineVertex::getBindingDescriptions();
  linesPipelineInfo.vertpath = "shaders/solidColorLines.vert.spv";
  linesPipelineInfo.fragpath = "shaders/solidColorLines.frag.spv";
  linesPipelineInfo.inputAssemblyInfo.topology =
      vk::PrimitiveTopology::eLineList;
  linesPipelineInfo.depthStencilInfo.depthCompareOp =
      vk::CompareOp::eGreaterOrEqual;
  linesPipelineInfo.subpass = 0;
  linesPipelineInfo.multisampleInfo.rasterizationSamples =
      static_cast<vk::SampleCountFlagBits>(context.vulkan.msaaSamples);
  linesPipeline = std::make_unique<GraphicsPipeline>(context, linesPipelineInfo,
                                                     "solid color lines");
}

void SolidColorSys::createDescriptors() {
  std::vector<vk::DescriptorBindingFlags> bindingFlags = {
      vk::DescriptorBindingFlagBits::ePartiallyBound};

  vk::DescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{};
  bindingFlagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
  bindingFlagsInfo.pBindingFlags = bindingFlags.data();

  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eCombinedImageSampler,
          static_cast<uint32_t>(maxTextures),
          vk::ShaderStageFlagBits::eFragment, nullptr}};

  texturesSetLayout = buildDescriptorSetLayout(
      context, bindings, vk::DescriptorSetLayoutCreateFlags{},
      &bindingFlagsInfo);

  debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkDescriptorSetLayout>(texturesSetLayout)),
                    "solidColor descriptor set layout");

  texturesSet =
      context.vulkan.globalDescriptorAllocator->allocate(texturesSetLayout);

  updateDescriptors();
}

void SolidColorSys::updateDescriptors() {
  if (images.empty())
    return;

  std::vector<vk::DescriptorImageInfo> imageInfos;
  imageInfos.reserve(maxTextures);

  for (const auto &img : images) {
    imageInfos.emplace_back(
        img.getDescriptorInfo(context.vulkan.defaultSampler));
  }

  vk::WriteDescriptorSet write{};
  write.dstSet = texturesSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = static_cast<uint32_t>(imageInfos.size());
  write.descriptorType = vk::DescriptorType::eCombinedImageSampler;
  write.setPImageInfo(imageInfos.data());

  context.vulkan.device.updateDescriptorSets(1, &write, 0, nullptr);
}

size_t SolidColorSys::addTextureFromPNGMemory(void *data, size_t size) {
  ImageCreateInfo_PNGdata info{};
  info.format = vk::Format::eR8G8B8A8Unorm;
  info.dataSize = size;
  info.data = data;

  images.emplace_back(context, info);
  updateDescriptors();

  return images.size() - 1;
}

size_t SolidColorSys::addTextureFromFile(std::filesystem::path path) {
  images.emplace_back(context, path);
  updateDescriptors();

  return images.size() - 1;
}

SolidColorSys::SolidColorSys(EngineContext &context) : System(context) {
  images.emplace_back(context, "textures/hud.png");
  images.reserve(maxTextures);

  createDescriptors();
  createBuffers();
  createPipelines();

  updateDescriptors();
}

SolidColorSys::~SolidColorSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(texturesSetLayout,
                                                     nullptr);
  }
}

void SolidColorSys::render(size_t lineIndicesSize, size_t triangleIndicesSize) {
  auto cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "SolidColorSys rendering",
                    glm::vec4{.9f, .1f, .1f, 1.f});

  debug::beginLabel(context, cmd, "lines", glm::vec4{.9f, .1f, .1f, 1.f});

  vk::DeviceSize offsets[] = {0};

  if (lineIndicesSize) {
    linesPipeline->bind(cmd);
    vk::Buffer linesBuffers[] = {*linesVertexBuffer};
    cmd.bindVertexBuffers(0, 1, linesBuffers, offsets);
    cmd.bindIndexBuffer(*linesIndexBuffer, 0, vk::IndexType::eUint32);

    std::array<vk::DescriptorSet, 2> sets = {
        context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
        texturesSet};
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *linesPipeline, 0,
                           static_cast<uint32_t>(sets.size()), sets.data(), 0,
                           nullptr);

    cmd.drawIndexed(static_cast<uint32_t>(lineIndicesSize), 1, 0, 0, 0);
  }
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "triangles", glm::vec4{.9f, .1f, .1f, 1.f});
  if (triangleIndicesSize) {
    trianglePipeline->bind(cmd);
    vk::Buffer trianglesBuffers[] = {*trianglesVertexBuffer};
    cmd.bindVertexBuffers(0, 1, trianglesBuffers, offsets);
    cmd.bindIndexBuffer(*trianglesIndexBuffer, 0, vk::IndexType::eUint32);

    std::array<vk::DescriptorSet, 2> sets = {
        context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
        texturesSet};
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics, *trianglePipeline,
                           0, static_cast<uint32_t>(sets.size()), sets.data(),
                           0, nullptr);
    cmd.drawIndexed(static_cast<uint32_t>(triangleIndicesSize), 1, 0, 0, 0);
  }
  debug::endLabel(context, cmd);
  debug::endLabel(context, cmd);
}

} // namespace vkh
