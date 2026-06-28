#include "particles.hpp"

#include "../buffer.hpp"
#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"
#include <vulkan/vulkan.hpp>

namespace vkh {

/* Bugged but the bug is cool
void ParticleSys::setAttractor(std::vector<glm::mat4> newTransformations) {
transformationCount = newTransformations.size();

transformationsBuffer = std::make_unique<Buffer<glm::mat4>>(
    context, vk::BufferUsageFlagBits::eStorageBuffer,
    vk::MemoryPropertyFlagBits::eHostVisible |
        vk::MemoryPropertyFlagBits::eHostCoherent,
    transformationCount);

transformationsBuffer->map();
transformationsBuffer->write(newTransformations.data());
transformationsBuffer->unmap();

setupDescriptorSet();
}*/

void ParticleSys::setAttractor(std::vector<glm::mat4> newTransformations) {
  if (newTransformations.size() > transformationCount + 3 ||
      !transformationsBuffer) {
    transformationsBuffer = std::make_unique<Buffer<glm::mat4>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        newTransformations.size() + 3);
    transformationsBuffer->map();
  }

  transformationCount = newTransformations.size();
  transformationsBuffer->write(newTransformations.data());

  setupDescriptorSet();
}

void ParticleSys::createVB() {
  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context,
      vk::BufferUsageFlagBits::eStorageBuffer |
          vk::BufferUsageFlagBits::eVertexBuffer,
      vk::MemoryPropertyFlagBits::eDeviceLocal, maxParticles);
}

void ParticleSys::setupDescriptorSet() {
  set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);

  DescriptorWriter writer(context);
  writer.writeBuffer(0, vertexBuffer->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(1, transformationsBuffer->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.updateSet(set);
}

void ParticleSys::setupDescriptorSetLayout() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
      vk::DescriptorSetLayoutBinding{1, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
  };
  setLayout = buildDescriptorSetLayout(context, bindings);
}

void ParticleSys::createPipeline() {
  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout};

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo graphicsPipelineInfo{};
  graphicsPipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  graphicsPipelineInfo.layoutInfo = pipelineLayoutInfo;
  graphicsPipelineInfo.inputAssemblyInfo.topology =
      vk::PrimitiveTopology::ePointList;
  graphicsPipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  graphicsPipelineInfo.attributeDescriptions =
      Vertex::getAttributeDescriptions();
  graphicsPipelineInfo.depthStencilInfo.depthWriteEnable = false;
  GraphicsPipeline::enableAlphaBlending(graphicsPipelineInfo);
  graphicsPipelineInfo.vertpath = "shaders/particles.vert.spv";
  graphicsPipelineInfo.fragpath = "shaders/particles.frag.spv";
  graphicsPipelineInfo.subpass = 1;
  graphicsPipeline = std::make_unique<GraphicsPipeline>(
      context, graphicsPipelineInfo, "particles graphics pipeline");

  vk::PushConstantRange pushConstantRange{vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(PushConstantData)};

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
  createVB();
  setupDescriptorSetLayout();
  createPipeline();
}

ParticleSys::~ParticleSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
  }
}

void ParticleSys::update() {
  if (transformationCount <= 0)
    return;

  auto &cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "particleSys compute",
                    glm::vec4{.3f, .3f, .5f, 1.f});
  computePipeline->bind(cmd);

  std::vector<vk::DescriptorSet> sets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], set};
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, computePipeline->getLayout(), 0,
      static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

  PushConstantData data{static_cast<uint32_t>(transformationCount)};
  cmd.pushConstants(computePipeline->getLayout(),
                    vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(PushConstantData), &data);

  cmd.dispatch(static_cast<uint32_t>(maxParticles / 256), 1, 1);

  vk::BufferMemoryBarrier barrier{vk::AccessFlagBits::eShaderWrite,
                                  vk::AccessFlagBits::eVertexAttributeRead,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  *vertexBuffer,
                                  0,
                                  VK_WHOLE_SIZE};

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eVertexInput,
                      vk::DependencyFlags{}, 0, nullptr, 1, &barrier, 0,
                      nullptr);

  debug::endLabel(context, cmd);
}

void ParticleSys::render() {
  if (transformationCount <= 0)
    return;

  auto &cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "particleSys render",
                    glm::vec4{.7f, .3f, .5f, 1.f});
  graphicsPipeline->bind(cmd);

  vk::DescriptorSet globalSet =
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex];
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         graphicsPipeline->getLayout(), 0, 1, &globalSet, 0,
                         nullptr);

  vk::Buffer buffers[] = {*vertexBuffer};
  vk::DeviceSize offsets[] = {0};
  cmd.bindVertexBuffers(0, 1, buffers, offsets);
  cmd.draw(maxParticles, 1, 0, 0);
  debug::endLabel(context, cmd);
}

} // namespace vkh
