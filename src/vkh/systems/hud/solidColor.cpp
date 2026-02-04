#include "solidColor.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <vulkan/vulkan_core.h>

#include "../../buffer.hpp"
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
void SolidColorSys::createPipelines() {
  PipelineCreateInfo trianglesPipelineInfo{};
  trianglesPipelineInfo.layoutInfo.setLayoutCount = 1;
  trianglesPipelineInfo.layoutInfo.pSetLayouts = &setLayout;
  trianglesPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  trianglesPipelineInfo.attributeDescriptions =
      TriangleVertex::getAttributeDescriptions();
  trianglesPipelineInfo.bindingDescriptions =
      TriangleVertex::getBindingDescriptions();
  trianglesPipelineInfo.vertpath = "shaders/solidColorTriangles.vert.spv";
  trianglesPipelineInfo.fragpath = "shaders/solidColorTriangles.frag.spv";
  trianglesPipelineInfo.depthStencilInfo.depthCompareOp =
      VK_COMPARE_OP_GREATER_OR_EQUAL;
  trianglePipeline = std::make_unique<GraphicsPipeline>(
      context, trianglesPipelineInfo, "solid color triangles");

  PipelineCreateInfo linesPipelineInfo{};
  linesPipelineInfo.layoutInfo.pSetLayouts =
      &context.vulkan.globalDescriptorSetLayout;
  linesPipelineInfo.layoutInfo.setLayoutCount = 1;
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
  linesPipeline = std::make_unique<GraphicsPipeline>(context, linesPipelineInfo,
                                                     "solid color lines");
}
void SolidColorSys::createDescriptors() {
  setLayout = buildDescriptorSetLayout(
      context, {VkDescriptorSetLayoutBinding{
                   .binding = 0,
                   .descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                   .descriptorCount = maxTextures,
                   .stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT,
               }});
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_SET_LAYOUT,
                    reinterpret_cast<uint64_t>(setLayout),
                    "solidColor descriptor set layout");
  std::vector<VkDescriptorImageInfo> imageInfos;
  for (const auto &img : images) {
    VkDescriptorImageInfo info = imageInfos.emplace_back(
        img.getDescriptorInfo(context.vulkan.defaultSampler));
  }
  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

  updateDescriptors();
}
void SolidColorSys::updateDescriptors() {
  std::vector<VkDescriptorImageInfo> imageInfos;
  imageInfos.reserve(maxTextures);

  for (const auto &img : images) {
    imageInfos.push_back(img.getDescriptorInfo(context.vulkan.defaultSampler));
  }

  const VkDescriptorImageInfo &fallback = imageInfos[0]; // hud.png

  while (imageInfos.size() < maxTextures) {
    imageInfos.push_back(fallback);
  }

  VkWriteDescriptorSet write{};
  write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
  write.dstSet = set;
  write.dstBinding = 0;
  write.dstArrayElement = 0;
  write.descriptorCount = maxTextures; // ← **full size**
  write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
  write.pImageInfo = imageInfos.data();

  vkUpdateDescriptorSets(context.vulkan.device, 1, &write, 0, nullptr);
}
unsigned short SolidColorSys::addTextureFromMemory(unsigned char *pixels,
                                                   glm::uvec2 size) {
  ImageCreateInfo info{};
  info.format = VK_FORMAT_R8G8B8A8_UNORM;
  info.size = size;
  info.data = reinterpret_cast<void *>(pixels);

  images.emplace_back(context, info);

  updateDescriptors();

  return static_cast<unsigned short>(images.size() - 1);
}
SolidColorSys::SolidColorSys(EngineContext &context) : System(context) {
  // 1. Load the HUD image, it becomes index 0 and fallback for unused texture
  // indexes
  images.emplace_back(context, "textures/hud.png");

  images.reserve(maxTextures);

  createDescriptors();
  createBuffers();
  createPipelines();

  updateDescriptors();
}

SolidColorSys::~SolidColorSys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, setLayout, nullptr);
}

void SolidColorSys::render(size_t lineVerticesSize,
                           size_t triangleIndicesSize) {
  debug::beginLabel(context, context.frameInfo.cmd, "SolidColorSys rendering",
                    glm::vec4{.9f, .1f, .1f, 1.f});
  debug::beginLabel(context, context.frameInfo.cmd, "lines",
                    glm::vec4{.9f, .1f, .1f, 1.f});
  VkDeviceSize offsets[] = {0};
  if (lineVerticesSize) {
    linesPipeline->bind(context.frameInfo.cmd);
    VkBuffer linesBuffers[] = {*linesVertexBuffer};
    vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, linesBuffers, offsets);
    vkCmdBindDescriptorSets(
        context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *linesPipeline,
        0, 1,
        &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
        nullptr);
    vkCmdDraw(context.frameInfo.cmd, static_cast<uint32_t>(lineVerticesSize), 1,
              0, 0);
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
    vkCmdBindDescriptorSets(context.frameInfo.cmd,
                            VK_PIPELINE_BIND_POINT_GRAPHICS, *trianglePipeline,
                            0, 1, &set, 0, nullptr);
    vkCmdDrawIndexed(context.frameInfo.cmd, triangleIndicesSize, 1, 0, 0, 0);
  }
  debug::endLabel(context, context.frameInfo.cmd);
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
