#include "particles.hpp"

#include "../buffer.hpp"
#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include <algorithm>
#include <vulkan/vulkan.hpp>

namespace vkh {

void ParticleSys::setAttractor(std::vector<glm::mat4> transforms) {
  transforms.resize(std::min<size_t>(transforms.size(), maxAttractors));
  attractors = std::move(transforms);
}
void ParticleSys::setEnvironment(std::vector<glm::vec3> positions) {
  positions.resize(std::min<size_t>(positions.size(), maxPatches));
  patches = std::move(positions);
}
void ParticleSys::createVB() {
  frames.resize(context.vulkan.maxFramesInFlight);
  for (auto &frame : frames) {
    frame.vertices = std::make_unique<Buffer<Vertex>>(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eVertexBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal, maxParticles);
    frame.transforms = std::make_unique<Buffer<glm::mat4>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        maxAttractors + maxPatches);
    frame.transforms->map();
  }
}
void ParticleSys::setupDescriptorSet() {
  for (auto &frame : frames) {
    frame.set = context.vulkan.globalDescriptorAllocator->allocate(setLayout);
    DescriptorWriter writer(context);
    writer.writeBuffer(0, frame.vertices->descriptorInfo(),
                       vk::DescriptorType::eStorageBuffer);
    writer.writeBuffer(1, frame.transforms->descriptorInfo(),
                       vk::DescriptorType::eStorageBuffer);
    writer.updateSet(frame.set);
  }
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
  graphicsPipelineInfo.layoutInfo = pipelineLayoutInfo;
  graphicsPipelineInfo.inputAssemblyInfo.topology =
      vk::PrimitiveTopology::eTriangleList;
  graphicsPipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  graphicsPipelineInfo.attributeDescriptions =
      Vertex::getAttributeDescriptions();
  graphicsPipelineInfo.depthStencilInfo.depthWriteEnable = false;
  graphicsPipelineInfo.rasterizationInfo.cullMode = vk::CullModeFlagBits::eNone;
  GraphicsPipeline::enableAlphaBlending(graphicsPipelineInfo);
  graphicsPipelineInfo.colorBlendAttachment.dstColorBlendFactor =
      vk::BlendFactor::eOne;
  graphicsPipelineInfo.vertpath = "shaders/particles.vert.spv";
  graphicsPipelineInfo.fragpath = "shaders/particles.frag.spv";
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
  setupDescriptorSet();
  createPipeline();
}

ParticleSys::~ParticleSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(setLayout, nullptr);
  }
}

void ParticleSys::update() {

  auto &frame = frames[context.frameInfo.frameIndex];
  std::vector<glm::mat4> dataTransforms(maxAttractors + patches.size(),
                                        glm::mat4{1.f});
  std::copy(attractors.begin(), attractors.end(), dataTransforms.begin());
  for (size_t i = 0; i < patches.size(); ++i)
    dataTransforms[maxAttractors + i][3] = glm::vec4(patches[i], 1.f);
  frame.transforms->write(dataTransforms.data(),
                          dataTransforms.size() * sizeof(glm::mat4));
  particleCount = patches.size() * 8 +
                  NightLights::activeCount * NightLights::insectsPerSwarm;
  auto &cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "particleSys compute",
                    glm::vec4{.3f, .3f, .5f, 1.f});
  computePipeline->bind(cmd);

  std::vector<vk::DescriptorSet> sets = {
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      frame.set};
  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eCompute, computePipeline->getLayout(), 0,
      static_cast<uint32_t>(sets.size()), sets.data(), 0, nullptr);

  PushConstantData data{uint32_t(attractors.size()), uint32_t(patches.size()),
                        particleCount, context.time};
  cmd.pushConstants(computePipeline->getLayout(),
                    vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(PushConstantData), &data);

  cmd.dispatch(static_cast<uint32_t>((particleCount + 255) / 256), 1, 1);

  vk::BufferMemoryBarrier barrier{vk::AccessFlagBits::eShaderWrite,
                                  vk::AccessFlagBits::eVertexAttributeRead,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  VK_QUEUE_FAMILY_IGNORED,
                                  *frame.vertices,
                                  0,
                                  VK_WHOLE_SIZE};

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eVertexInput,
                      vk::DependencyFlags{}, 0, nullptr, 1, &barrier, 0,
                      nullptr);

  debug::endLabel(context, cmd);
}

void ParticleSys::render() {
  if (particleCount == 0)
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

  vk::Buffer buffers[] = {*frames[context.frameInfo.frameIndex].vertices};
  vk::DeviceSize offsets[] = {0};
  cmd.bindVertexBuffers(0, 1, buffers, offsets);
  cmd.draw(6, particleCount, 0, 0);
  debug::endLabel(context, cmd);
}

} // namespace vkh
