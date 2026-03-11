#include "particles.hpp"
#include <GLFW/glfw3.h>
#include <cstdlib>
#include <random>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>
#include <vulkan/vulkan_core.h>

#include <algorithm>
#include <memory>

#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

namespace vkh {

void ParticleSys::createBuffer() {
  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context,
      VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, maxParticles);
}
void ParticleSys::setupDescriptors() {
  std::vector<VkDescriptorSetLayoutBinding> bindings = {
      VkDescriptorSetLayoutBinding{
          .binding = 0,
          .descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
          .descriptorCount = 1,
          .stageFlags = VK_SHADER_STAGE_COMPUTE_BIT,
      },
  };
  setLayout = buildDescriptorSetLayout(context, bindings);

  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

  DescriptorWriter writer(context);
  writer.writeBuffer(0, vertexBuffer->descriptorInfo(),
                     VK_DESCRIPTOR_TYPE_STORAGE_BUFFER);
  writer.updateSet(set);
}
void ParticleSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo graphicsPipelineInfo{};
  graphicsPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  graphicsPipelineInfo.layoutInfo = pipelineLayoutInfo;
  graphicsPipelineInfo.inputAssemblyInfo.topology =
      VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  graphicsPipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  graphicsPipelineInfo.attributeDescriptions =
      Vertex::getAttributeDescriptions();
  graphicsPipelineInfo.depthStencilInfo.depthWriteEnable = false;
  GraphicsPipeline::enableAlphaBlending(graphicsPipelineInfo);
  graphicsPipelineInfo.vertpath = "shaders/particles.vert.spv";
  graphicsPipelineInfo.fragpath = "shaders/particles.frag.spv";
  graphicsPipeline = std::make_unique<GraphicsPipeline>(
      context, graphicsPipelineInfo, "particles graphics pipeline");

  VkPushConstantRange pushConstantRange{};
  pushConstantRange.size = sizeof(PushConstantData);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;

  setLayouts.emplace_back(setLayout);
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  computePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/particles.comp.spv", pipelineLayoutInfo,
      "particles compute pipeline");
}
ParticleSys::ParticleSys(EngineContext &context) : System(context) {
  createBuffer();
  setupDescriptors();
  createPipeline();
}

ParticleSys::~ParticleSys() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, setLayout, nullptr);
}

void ParticleSys::update() {
  debug::beginLabel(context, context.frameInfo.cmd, "particleSys compute",
                    glm::vec4{.3f, .3f, .5f, 1.f});
  computePipeline->bind(context.frameInfo.cmd);

  VkDescriptorSet sets[] = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], set};
  vkCmdBindDescriptorSets(context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          *computePipeline, 0, 2, sets, 0, nullptr);

  PushConstantData data{glm::vec3{1.f}, context.frameInfo.dt};
  vkCmdPushConstants(context.frameInfo.cmd, *computePipeline,
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantData),
                     &data);

  vkCmdDispatch(context.frameInfo.cmd, 256, 1, 1);

  VkBufferMemoryBarrier barrier{};
  barrier.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
  barrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  barrier.dstAccessMask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT;
  barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
  barrier.buffer = *vertexBuffer;
  barrier.offset = 0;
  barrier.size = VK_WHOLE_SIZE;

  vkCmdPipelineBarrier(context.frameInfo.cmd,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_VERTEX_INPUT_BIT, 0, 0, nullptr, 1,
                       &barrier, 0, nullptr);

  debug::endLabel(context, context.frameInfo.cmd);
}

void ParticleSys::render() {
  debug::beginLabel(context, context.frameInfo.cmd, "particleSys render",
                    glm::vec4{.7f, .3f, .5f, 1.f});
  graphicsPipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(
      context.frameInfo.cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, *graphicsPipeline,
      0, 1, &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      0, nullptr);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  vkCmdDraw(context.frameInfo.cmd, maxParticles, 1, 0, 0);
  debug::endLabel(context, context.frameInfo.cmd);
}
} // namespace vkh
