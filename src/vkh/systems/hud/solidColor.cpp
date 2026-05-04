#include "solidColor.hpp"

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../pipeline.hpp"
#include "../../swapChain.hpp"

namespace vkh {
void SolidColorSys::createBuffers() {
  linesVertexBuffer = std::make_unique<Buffer<LineVertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxLineVertexCount);
  linesVertexBuffer->map();

  linesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxLineIndexCount);
  linesIndexBuffer->map();

  trianglesVertexBuffer = std::make_unique<Buffer<TriangleVertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxTriangleVertexCount);
  trianglesVertexBuffer->map();

  trianglesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
      context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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
    vkDeviceWaitIdle(context.vulkan.device);
    linesVertexBuffer = std::make_unique<Buffer<LineVertex>>(
        context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        maxLineVertexCount);
    linesVertexBuffer->map();

    linesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
        context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
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
    vkDeviceWaitIdle(context.vulkan.device);
    trianglesVertexBuffer = std::make_unique<Buffer<TriangleVertex>>(
        context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        maxTriangleVertexCount);
    trianglesVertexBuffer->map();

    trianglesIndexBuffer = std::make_unique<Buffer<uint32_t>>(
        context, VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
            VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        maxTriangleIndexCount);
    trianglesIndexBuffer->map();
  }
}

void SolidColorSys::createPipelines() {
  std::array<VkDescriptorSetLayout, 2> setLayouts{
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
      VK_COMPARE_OP_GREATER_OR_EQUAL;
  trianglesPipelineInfo.subpass = 0;
  trianglesPipelineInfo.multisampleInfo.rasterizationSamples =
      context.vulkan.msaaSamples;
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
      VK_PRIMITIVE_TOPOLOGY_LINE_LIST;
  linesPipelineInfo.depthStencilInfo.depthCompareOp =
      VK_COMPARE_OP_GREATER_OR_EQUAL;
  linesPipelineInfo.subpass = 0;
  linesPipelineInfo.multisampleInfo.rasterizationSamples =
      context.vulkan.msaaSamples;
  linesPipeline = std::make_unique<GraphicsPipeline>(context, linesPipelineInfo,
                                                     "solid color lines");
}
void SolidColorSys::createDescriptors() {
  VkDescriptorSetLayoutBindingFlagsCreateInfo bindingFlagsInfo{
      .sType =
          VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_BINDING_FLAGS_CREATE_INFO};
  VkDescriptorBindingFlags bindingFlags[] = {
      VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT};
  bindingFlagsInfo.pBindingFlags = bindingFlags;
  bindingFlagsInfo.bindingCount = 1;
  texturesSetLayout = buildDescriptorSetLayout(
      context,
      {
          VkDescriptorSetLayoutBinding{
              .binding = 0,
              .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
              .descriptorCount = maxTextures,
              .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
          },
      },
      0, &bindingFlagsInfo);
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(texturesSetLayout),
                    "solidColor descriptor set layout");
  texturesSet =
      context.vulkan.globalDescriptorAllocator->allocate(texturesSetLayout);

  updateDescriptors();
}
void SolidColorSys::updateDescriptors() {
  if (images.empty())
    return;

  std::vector<VkDescriptorImageInfo> imageInfos;
  imageInfos.reserve(maxTextures);

  for (const auto &img : images) {
    imageInfos.emplace_back(
        img.getDescriptorInfo(context.vulkan.defaultSampler));
  }

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = texturesSet;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = imageInfos.size();
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = imageInfos.data();

  vkUpdateDescriptorSets(context.vulkan.device, 1, &write, 0, nullptr);
}
size_t SolidColorSys::addTextureFromPNGMemory(void *data, size_t size) {
  ImageCreateInfo_PNGdata info{};
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
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
  // Load the HUD image, it becomes index 0 and fallback for unused texture
  // indexes
  images.emplace_back(context, "textures/hud.png");

  images.reserve(maxTextures);

  createDescriptors();
  createBuffers();
  createPipelines();

  updateDescriptors();
}

SolidColorSys::~SolidColorSys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, texturesSetLayout,
                               nullptr);
}

void SolidColorSys::render(size_t lineIndicesSize, size_t triangleIndicesSize) {
  debug::beginLabel(context, context.frameInfo.cmd, "SolidColorSys rendering",
                    glm::vec4{.9f, .1f, .1f, 1.f});

  debug::beginLabel(context, context.frameInfo.cmd, "lines",
                    glm::vec4{.9f, .1f, .1f, 1.f});
  VkDeviceSize offsets[] = {0};
  if (lineIndicesSize) {
    linesPipeline->bind(context.frameInfo.cmd);
    VkBuffer linesBuffers[] = {*linesVertexBuffer};
    vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, linesBuffers, offsets);
    vkCmdBindIndexBuffer(context.frameInfo.cmd, *linesIndexBuffer, 0,
                         VK_INDEX_TYPE_UINT32);

    std::array<VkDescriptorSet, 2> sets = {
        context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
        texturesSet};
    vkCmdBindDescriptorSets(
        context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *linesPipeline,
        0, static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

    // Switch to Indexed Draw
    vkCmdDrawIndexed(context.frameInfo.cmd,
                     static_cast<uint32_t>(lineIndicesSize), 1, 0, 0, 0);
  }
  debug::endLabel(context, context.frameInfo.cmd);

  debug::beginLabel(context, context.frameInfo.cmd, "triangles",
                    glm::vec4{.9f, .1f, .1f, 1.f});
  if (triangleIndicesSize) {
    trianglePipeline->bind(context.frameInfo.cmd);
    VkBuffer trianglesBuffers[] = {*trianglesVertexBuffer};
    vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, trianglesBuffers,
                           offsets);
    vkCmdBindIndexBuffer(context.frameInfo.cmd, *trianglesIndexBuffer, 0,
                         VK_INDEX_TYPE_UINT32);
    std::array<VkDescriptorSet, 2> sets = {
        context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
        texturesSet};
    vkCmdBindDescriptorSets(context.frameInfo.cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS, *trianglePipeline,
                            0, static_cast<uint32_t>(sets.size()), sets.data(),
                            0, nullptr);
    vkCmdDrawIndexed(context.frameInfo.cmd, triangleIndicesSize, 1, 0, 0, 0);
  }
  debug::endLabel(context, context.frameInfo.cmd);
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
