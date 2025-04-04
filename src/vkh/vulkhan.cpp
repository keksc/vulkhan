#include "vulkhan.hpp"
#include "deviceHelpers.hpp"
#include <fmt/core.h>
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

#include "AxisAlignedBoundingBox.hpp"
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
#include "systems/hud.hpp"
#include "systems/particles.hpp"
#include "systems/physics.hpp"
#include "systems/water/water.hpp"
#include "window.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace vkh {
void loadObjects(EngineContext &context) {
  auto &playerTransform = context.entities[0].transform;
  context.entities.push_back(
      {context,
       {.position = {.5f, .5f, .5f}, .scale = {.5f, .5f, .5f}},
       "models/sword.glb"});

  context.entities.push_back(
      {context,
       {.position = {5.f, .5f, .5f}, .scale = {.5f, .5f, .5f}},
       "models/westwingassets.glb"});
  std::vector<glm::vec3> lightColors{{1.f, .1f, .1f}, {.1f, .1f, 1.f},
                                     {.1f, 1.f, .1f}, {1.f, 1.f, .1f},
                                     {.1f, 1.f, 1.f}, {1.f, 1.f, 1.f}};

  for (int i = 0; i < lightColors.size(); i++) {
    auto rotateLight = glm::rotate(
        glm::mat4(1.f), (i * glm::two_pi<float>()) / lightColors.size(),
        {0.f, 1.f, 0.f});
    context.particles.push_back(
        {.position = rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f),
         .color = glm::vec4(lightColors[i], 1.f)});
  }
}
void updateObjs(EngineContext &context) {}
void updateParticles(EngineContext &context, GlobalUbo &ubo) {
  auto rotateParticle = glm::rotate(glm::mat4(1.f), 0.5f * context.frameInfo.dt,
                                    {0.f, -1.f, 0.f});
  int particleIndex = 0;
  for (int i = 0; i < 6; i++) {
    assert(particleIndex < MAX_PARTICLES &&
           "Point lights exceed maximum specified");

    // update light position
    context.particles[i].position = glm::vec4(
        rotateParticle * glm::vec4(context.particles[i].position, 1.f));

    // copy light to ubo
    ubo.particles[particleIndex].position = context.particles[i].position;
    ubo.particles[particleIndex].color = context.particles[i].color;

    particleIndex += 1;
  }
  ubo.numParticles = particleIndex;
}

void run() {
  EngineContext context{};
  initWindow(context);
  initVulkan(context);
  // audio::init();
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkah is cleaned up
    input::init(context);

    context.vulkan.globalDescriptorPool =
        DescriptorPool::Builder(context)
            .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT + MAX_SAMPLERS)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                         SwapChain::MAX_FRAMES_IN_FLIGHT + 90)
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
        {context, {.position = {0.f, GROUND_LEVEL + 10.f, 0.f}}});
    loadObjects(context);

    // std::thread(generateDungeon, std::ref(context)).detach();

    waterSys::SkyParams skyParams;
    SkyPreetham sky({0.f, 3.f, .866f});
    skyParams.props = sky.GetProperties();

    entitySys::init(context);
    axesSys::init(context);
    particleSys::init(context);
    freezeAnimationSys::init(context);
    waterSys::init(context);

    waterSys::createRenderData(context, context.vulkan.swapChain->imageCount());
    auto cmd = beginSingleTimeCommands(context);
    waterSys::prepare(context, cmd);
    endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
    fontSys::init(context);
    hudSys::init(context);

    std::vector<VkDescriptorSet> globalDescriptorSets(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < globalDescriptorSets.size(); i++) {
      auto bufferInfo = uboBuffers[i]->descriptorInfo();
      DescriptorWriter(*context.vulkan.globalDescriptorSetLayout,
                       *context.vulkan.globalDescriptorPool)
          .writeBuffer(0, &bufferInfo)
          .build(globalDescriptorSets[i]);
    }

    hudSys::View hudWorld(context);
    hudWorld.addElement<hudSys::Rect>(
        glm::vec2{-.4f, -.4f}, glm::vec2{.5f, .5f}, glm::vec3{.5f, .5f, .5f});

    hudSys::View hudPause(context);
    hudPause.addElement<hudSys::Button>(
        glm::vec2{-.5f, -.5f}, glm::vec2{.3f, .3f} * context.window.aspectRatio,
        glm::vec3{1.f, 1.f, 1.f}, [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            fmt::println("nice");
            // hudWorld.setCurrent();
          }
        });
    hudPause.setCurrent();
    // btn->addChild(std::make_shared<hudSys::Rect>(
    //     glm::vec2{.1f, .1f}, glm::vec2{.8f, .8f}, glm::vec3{1.f, 0.f, 0.f}));

    auto currentTime = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(context.window)) {
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();
      fontSys::updateText(
          context, fmt::format("FPS: {}", static_cast<int>(1.f / frameTime)));
      currentTime = newTime;

      input::moveInPlaneXZ(context);
      context.camera.position = context.entities[0].transform.position +
                                glm::vec3{0.f, camera::HEIGHT, 0.f};
      context.camera.orientation = context.entities[0].transform.orientation;

      // audio::update(context);

      float aspect = context.window.aspectRatio;

      context.camera.projectionMatrix =
          glm::perspective(glm::radians(60.f), aspect, .1f, 1000.f);
      context.camera.projectionMatrix[1][1] *= -1; // Flip Y for Vulkan
      context.camera.projectionMatrix[0][0] *= -1; // Flip X for rotation
      camera::calcViewYXZ(context);

      hudSys::update(context, hudWorld);
      if (auto commandBuffer = renderer::beginFrame(context)) {
        int frameIndex = renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
            globalDescriptorSets[frameIndex],
        };

        // update
        if (context.view == EngineContext::World) {
          updateObjs(context);
          GlobalUbo ubo{};
          ubo.projection = context.camera.projectionMatrix;
          ubo.view = context.camera.viewMatrix;
          ubo.inverseView = context.camera.inverseViewMatrix;
          ubo.aspectRatio = aspect;
          if (glfwGetKey(context.window, GLFW_KEY_G))
            physicsSys::update(context);
          updateParticles(context, ubo);
          if (glfwGetKey(context.window, GLFW_KEY_U)) {
            skyParams.props.sunDir.x += .1f * context.frameInfo.dt;
            sky.update();
          }
          uboBuffers[frameIndex]->write(&ubo);
          uboBuffers[frameIndex]->flush();
        } else {
          hudSys::update(context, hudPause);
        }

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        hudSys::render(context);
        fontSys::render(context);
        if (context.view == EngineContext::World) {
          entitySys::render(context);
          axesSys::render(context);
          // freezeAnimationSys::render(context);
          // waterSys::update(context);
          // waterSys::prepareRender(context, skyParams);
          // waterSys::render(context);

          particleSys::render(context);
        }

        renderer::endSwapChainRenderPass(commandBuffer);
        renderer::endFrame(context);
      }
    }

    vkDeviceWaitIdle(context.vulkan.device);

    entitySys::cleanup(context);
    axesSys::cleanup(context);
    freezeAnimationSys::cleanup(context);
    particleSys::cleanup(context);
    waterSys::cleanup();
    fontSys::cleanup(context);
    hudSys::cleanup(context);

    context.entities.clear();
    context.vulkan.globalDescriptorSetLayout = nullptr;
    context.vulkan.modelDescriptorSetLayout = nullptr;
    context.vulkan.globalDescriptorPool = nullptr;
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
  // audio::cleanup();
}
} // namespace vkh
