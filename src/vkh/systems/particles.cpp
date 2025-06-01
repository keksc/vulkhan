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

#include <fmt/format.h>

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
  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      *context.vulkan.globalDescriptorSetLayout};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();

  PipelineCreateInfo pipelineConfig{};
  GraphicsPipeline::enableAlphaBlending(pipelineConfig);
  pipelineConfig.renderPass = context.vulkan.swapChain->renderPass;
  pipelineConfig.layoutInfo = pipelineLayoutInfo;
  pipelineConfig.inputAssemblyInfo.topology = VK_PRIMITIVE_TOPOLOGY_POINT_LIST;
  pipelineConfig.bindingDescriptions = Vertex::getBindingDescriptions();
  pipelineConfig.attributeDescriptions = Vertex::getAttributeDescriptions();
  GraphicsPipeline::enableAlphaBlending(pipelineConfig);
  pipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/particles.vert.spv", "shaders/particles.frag.spv",
      pipelineConfig);
}
ParticleSys::ParticleSys(EngineContext &context)
    : System(context), rng(rd()), rand(0.f, 1.f) {
  createPipeline();
  createBuffer();
}

void ParticleSys::update() {
  std::vector<Particle> newborns;
  newborns.reserve(64);

  auto it = particles.begin();
  while (it != particles.end()) {
    if (glfwGetTime() > it->timeOfDeath) {
      if (it->onDeath)
        it->onDeath(*it);
      // erase returns the next valid iterator
      it = particles.erase(it);
    } else {
      it->velocity.y -= 9.81f * context.frameInfo.dt;
      it->velocity *= 0.98f;
      it->pos += it->velocity * context.frameInfo.dt;
      ++it;
    }
  }

  // Emitters
  // particles.push_back(
  //     {{},
  //      glm::vec3{rand(rng), rand(rng), rand(rng)},
  //      glm::normalize(glm::vec3{rand(rng), 1.f, rand(rng)} * 2.f - 1.f)
  //      * 10.f, static_cast<float>(glfwGetTime()) + rand(rng) * 5.f + 2.f});

  for (int i = 0; i < 20; i++) {
    Particle newParticle{};
    newParticle.pos = {3.f + rand(rng) * 0.5f, 100.f, rand(rng) * 0.5f};
    newParticle.col = glm::vec3{.56f, .09f, .03f} + (rand(rng) - .5f) * .3f;
    newParticle.velocity =
        glm::normalize(glm::vec3{rand(rng) - 0.5f, 1.f, rand(rng) - 0.5f}) *
        (5.f + rand(rng) * 5.f);
    newParticle.timeOfDeath = glfwGetTime() + 2.f;

    newParticle.onDeath = [&](Particle &parent) {
      for (int j = 0; j < 6; j++) {
        Particle spark{};
        spark.pos = parent.pos;
        spark.velocity = glm::normalize(glm::vec3{rand(rng) - 0.5f, rand(rng),
                                                  rand(rng) - 0.5f}) *
                         (2.f + rand(rng) * 2.f);
        spark.col = glm::vec3{.54f, .33f, .5f} + (rand(rng) - .5f) * .3f;
        spark.timeOfDeath = glfwGetTime() + 0.3f + rand(rng) * 0.2f;
        newborns.push_back(spark);
      }
    };

    particles.push_back(std::move(newParticle));
  }

  if (!newborns.empty()) {
    particles.insert(particles.end(), std::make_move_iterator(newborns.begin()),
                     std::make_move_iterator(newborns.end()));
  }

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
                          &context.frameInfo.globalDescriptorSet, 0, nullptr);

  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.cmd, 0, 1, buffers,
                         offsets);
  vkCmdDraw(context.frameInfo.cmd, particles.size(), 1, 0, 0);
}
} // namespace vkh
