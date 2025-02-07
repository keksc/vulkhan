#include "vulkhan.hpp"
#include <vulkan/vulkan_core.h>

#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <AL/al.h>
#include <fmt/format.h>
#include <glm/gtx/quaternion.hpp>

#include "audio.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "cleanupVulkan.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "entity.hpp"
#include "initVulkan.hpp"
#include "input.hpp"
#include "renderer.hpp"
#include "swapChain.hpp"
#include "systems/axes.hpp"
#include "systems/entities.hpp"
#include "systems/font.hpp"
#include "systems/freezeAnimation.hpp"
#include "systems/particles.hpp"
#include "systems/physics.hpp"
#include "systems/water.hpp"
#include "window.hpp"

#include <array>
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

using namespace std::string_literals;

namespace vkh {
const glm::vec3 daggerOffset = {0.5f, -0.5f, 1.2f};
void loadObjects(EngineContext &context) {
  auto &playerTransform = context.entities[0].transform;
  /*glm::vec3 daggerOffsetWorld = playerTransform.orientation * daggerOffset;
  glm::vec3 daggerPosition = playerTransform.position + daggerOffsetWorld;
  glm::quat daggerOrientation =
      playerTransform.orientation *
      glm::angleAxis(glm::pi<float>() * -0.5f, glm::vec3(0.0f, 1.0f, 0.0f)) *
      glm::angleAxis(glm::pi<float>() * 0.5f, glm::vec3(1.0f, 0.0f, 0.0f));*/
  ModelCreateInfo modelInfo{};
  modelInfo.filepath = "models/sword.glb";
  context.entities.push_back({context,
                              {.position = {0.5, -0.5, 0.5}/*daggerPosition*/,
                               .scale = {0.5f, 0.5f, 0.5f}/*,
                               .orientation = daggerOrientation*/},
                              modelInfo});

  /*model = Model::createModelFromFile(context, "floor", "models/quad.obj");
  context.entities.push_back(
      {.transform = {.position{0.f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = model});*/

  std::vector<glm::vec3> lightColors{{1.f, .1f, .1f}, {.1f, .1f, 1.f},
                                     {.1f, 1.f, .1f}, {1.f, 1.f, .1f},
                                     {.1f, 1.f, 1.f}, {1.f, 1.f, 1.f}};

  for (int i = 0; i < lightColors.size(); i++) {
    auto rotateLight = glm::rotate(
        glm::mat4(1.f), (i * glm::two_pi<float>()) / lightColors.size(),
        {0.f, -1.f, 0.f});
    context.particles.push_back(
        {.position = rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f),
         .color = glm::vec4(lightColors[i], 1.0f)});
  }

  /*modelInfo.filepath = "models/mujina.glb";
  context.entities.push_back(
      {context,
       {.position = {0.f, GROUND_LEVEL, 1.f},
        .orientation = glm::angleAxis(glm::pi<float>() * 0.5f,
                                      glm::vec3(0.0f, 1.0f, 0.0f)) *
                       glm::angleAxis(glm::pi<float>() * 1.f,
                                      glm::vec3(0.0f, 0.0f, 1.0f))},
       modelInfo});
  modelInfo.filepath = "models/ghostface.glb";
  context.entities.push_back(
      {context,
       {.position = {5.f, GROUND_LEVEL, 1.f},
        .orientation = glm::angleAxis(glm::pi<float>() * 0.5f,
                                      glm::vec3(0.0f, 1.0f, 0.0f)) *
                       glm::angleAxis(glm::pi<float>() * 1.f,
                                      glm::vec3(0.0f, 0.0f, 1.0f))},
       modelInfo});
  modelInfo.filepath = "models/ghost.glb";
  context.entities.push_back(
      {context,
       {.position = {0.f, GROUND_LEVEL, 5.f},
        .orientation = glm::angleAxis(glm::pi<float>() * 0.5f,
                                      glm::vec3(0.0f, 1.0f, 0.0f)) *
                       glm::angleAxis(glm::pi<float>() * 1.f,
                                      glm::vec3(0.0f, 0.0f, 1.0f))},
       modelInfo});
  context.particles.push_back({{1.0f, -2.0f, -1.0f}, {1.0f, 1.0f, 1.0f}});*/
}
void updateObjs(EngineContext &context) {
  /*auto &dagger = context.entities[1];
  auto &playerTransform = context.entities[0].transform;
  glm::vec3 daggerOffsetWorld = playerTransform.orientation * daggerOffset;
  glm::vec3 daggerPosition = playerTransform.position + daggerOffsetWorld;
  glm::quat daggerOrientation =
      playerTransform.orientation *
      glm::angleAxis(glm::pi<float>() * -0.5f, glm::vec3(0.0f, 1.0f, 0.0f));
  dagger.transform.position = playerTransform.position + daggerOffsetWorld;
  dagger.transform.orientation = daggerOrientation;*/
}
void run() {
  EngineContext context{};
  initWindow(context);
  initVulkan(context);
  initAudio();
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkah is cleaned up
    input::init(context);

    context.vulkan.globalPool =
        DescriptorPool::Builder(context)
            .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT + MAX_SAMPLERS)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                         SwapChain::MAX_FRAMES_IN_FLIGHT)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                         MAX_SAMPLERS)
            .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_STORAGE_IMAGES)
            .build();

    std::vector<std::unique_ptr<Buffer>> uboBuffers(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < uboBuffers.size(); i++) {
      BufferCreateInfo bufInfo{};
      bufInfo.instanceSize = sizeof(GlobalUbo);
      bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
      bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
      uboBuffers[i] = std::make_unique<Buffer>(context, bufInfo);
      uboBuffers[i]->map();
    }

    context.vulkan.globalDescriptorSetLayout =
        DescriptorSetLayout::Builder(context)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT |
                            VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();
    context.vulkan.modelDescriptorSetLayout =
        DescriptorSetLayout::Builder(context)
            .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

    context.entities.push_back(
        {context, {.position = {0.f, GROUND_LEVEL, 0.f}}});
    loadObjects(context);

    entitySys::init(context);
    axesSys::init(context);
    particleSys::init(context);
    freezeAnimationSys::init(context);
    waterSys::init(context);
    fontSys::init(context);

    std::vector<VkDescriptorSet> globalDescriptorSets(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < globalDescriptorSets.size(); i++) {
      auto bufferInfo = uboBuffers[i]->descriptorInfo();
      DescriptorWriter(*context.vulkan.globalDescriptorSetLayout,
                       *context.vulkan.globalPool)
          .writeBuffer(0, &bufferInfo)
          .build(globalDescriptorSets[i]);
    }

    auto currentTime = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(context.window)) {
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();
      // fmt::print("FPS: {}\n", static_cast<int>(1.f / frameTime));
      fontSys::updateText(context, "FPS: "s + std::to_string(1.f / frameTime));
      currentTime = newTime;

      input::moveInPlaneXZ(context);
      context.camera.position = context.entities[0].transform.position +
                                glm::vec3{0.f, camera::HEIGHT, 0.f};
      context.camera.orientation = context.entities[0].transform.orientation;
      camera::calcViewYXZ(context);

      updateAudio(context);

      float aspect = context.window.aspectRatio;
      camera::calcPerspectiveProjection(context, glm::radians(50.f), aspect,
                                        0.1f, 1000.f);

      if (auto commandBuffer = renderer::beginFrame(context)) {
        int frameIndex = renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
            globalDescriptorSets[frameIndex],
        };

        // update
        updateObjs(context);
        GlobalUbo ubo{};
        ubo.projection = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.aspectRatio = aspect;
        if (glfwGetKey(context.window, GLFW_KEY_G))
          physicsSys::update(context);
        particleSys::update(context, ubo);
        uboBuffers[frameIndex]->write(&ubo);
        uboBuffers[frameIndex]->flush();

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        entitySys::render(context);
        axesSys::render(context);
        // freezeAnimationSys::render(context);
        particleSys::render(context);
        waterSys::render(context);
        fontSys::render(context);

        renderer::endSwapChainRenderPass(commandBuffer);
        renderer::endFrame(context);
      }
    }

    vkDeviceWaitIdle(context.vulkan.device);

    entitySys::cleanup(context);
    axesSys::cleanup(context);
    freezeAnimationSys::cleanup(context);
    particleSys::cleanup(context);
    waterSys::cleanup(context);
    fontSys::cleanup(context);

    context.entities.clear();
    context.vulkan.globalDescriptorSetLayout = nullptr;
    context.vulkan.modelDescriptorSetLayout = nullptr;
    context.vulkan.globalPool = nullptr;
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
  cleanupAudio();
}
} // namespace vkh
