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

#include "../descriptors.hpp"
#include "../pipeline.hpp"
#include "../swapChain.hpp"

namespace vkh {

void ParticleSys::createBuffer() {
  VkDeviceSize verticesSize = sizeof(Vertex) * maxParticles;

  vertexBuffer = std::make_unique<Buffer<Vertex>>(
      context, VK_BUFFER_USAGE_VERTEX_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      maxParticles);
}
void ParticleSys::createPipeline() {
  std::vector<VkDescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.renderPass = context.vulkan.swapChain->renderPass;
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  pipelineInfo.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineInfo.attributeDescriptions = Vertex::getAttributeDescriptions();
  GraphicsPipeline::enableAlphaBlending(pipelineInfo);
  pipelineInfo.vertpath = "shaders/particles.vert.spv";
  pipelineInfo.fragpath = "shaders/particles.frag.spv";
  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "particles pipeline");
}
ParticleSys::ParticleSys(EngineContext &context)
    : System(context) {
  createPipeline();
  createBuffer();
}

void ParticleSys::update() {
  std::vector<Vertex> vertices(particles.size());
  for (int i = 0; i < vertices.size(); i++) {
    vertices[i].pos = particles[i].pos;
    vertices[i].col = particles[i].col;
  }
  std::sort(vertices.begin(), vertices.end(), [](Vertex a, Vertex b) {
    return glm::length2(a.pos) > glm::length2(b.pos);
  });
  vertexBuffer->map();
  vertexBuffer->write(vertices.data(), vertices.size() * sizeof(Vertex));
  vertexBuffer->unmap();
}

void ParticleSys::render() {
  pipeline->bind(context.frameInfo.cmd);

  vkCmdBindDescriptorSets(context.frameInfo.cmd,
                          VK_PIPELINE_BIND_POINT_GRAPHICS, *pipeline, 0, 1,
                          &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0, nullptr);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers, offsets);
  vkCmdDraw(context.frameInfo.cmd, particles.size(), 1, 0, 0);
}
} // namespace vkh
