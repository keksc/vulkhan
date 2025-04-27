#include "vulkhan.hpp"
#include "systems/hud/hudElements.hpp"
#include <vulkan/vulkan_core.h>

#include <GLFW/glfw3.h>
#include <fmt/format.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtx/norm.hpp>
#include <glm/gtx/quaternion.hpp>

#include "../dungeonGenerator.hpp"
#include "AxisAlignedBoundingBox.hpp"
#include "audio.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "cleanupVulkan.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "initVulkan.hpp"
#include "input.hpp"
#include "renderer.hpp"
#include "swapChain.hpp"
#include "systems/entity/entities.hpp"
#include "systems/freezeAnimation.hpp"
#include "systems/hud/hud.hpp"
#include "systems/particles.hpp"
#include "systems/water/water.hpp"
#include "window.hpp"

#include <chrono>
#include <memory>
#include <thread>
#include <vector>

namespace vkh {
void run() {
  EngineContext context{};
  initWindow(context);
  initVulkan(context);
  audio::init();
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkan is cleaned up
    context.camera.position = {0.f, 2.f, -2.5f};
    context.camera.orientation = {0.f, 0.f, -1.f, 0.f};

    WaterSys::SkyParams skyParams;
    SkyPreetham sky({0.f, 3.f, .866f});
    skyParams.props = sky.GetProperties();

    std::vector<EntitySys::Entity> entities;
    EntitySys entitySys(context, entities);
    entitySys.addEntity({.position = {.5f, .5f, .5f}, .scale = {.5f, .5f, .5f}},
                        "models/sword.glb", {});

    entitySys.addEntity({.position = {5.f, .5f, .5f}, .scale = {.5f, .5f, .5f}},
                        "models/westwingassets.glb", {});

    generateDungeon(entitySys);

    ParticleSys particleSys(context);
    FreezeAnimationSys freezeAnimationSys(context);
    // WaterSys waterSys(context);
    HudSys hudSys(context);

    hud::View hudWorld(context);
    hud::View hudPause(context);
    auto rect = hudWorld.addElement<hud::Rect>(glm::vec2{-1.f, -1.f},
                                               glm::vec2{.3f, .3f},
                                               glm::vec3{.678f, .007f, .388f});
    auto fpstxt = rect->addChild<hud::Text>(glm::vec2{0.f, 0.f});
    auto orientationtxt = hudWorld.addElement<hud::Text>(glm::vec2{1.f, -1.f});
    hudWorld.addElement<hud::Button>(
        glm::vec2{-.5f, -.5f}, glm::vec2{.3f, .3f}, glm::vec3{1.f, .5f, 1.f},
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            hudSys.setView(&hudPause);
          }
        });

    auto btn = hudPause.addElement<hud::Button>(
        glm::vec2{.1f, .1f}, glm::vec2{.8f, .8f}, glm::vec3{1.f, .5f, 1.f},
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            audio::play("sounds/ui.wav");
            hudSys.setView(&hudWorld);
          }
        });
    auto slider = hudPause.addElement<hud::Slider>(
        glm::vec2{-1.f, -1.f}, glm::vec2{.5f, .1f}, glm::vec3{.5f, .5f, .8f},
        glm::vec2{0.f, 1.f}, .9f);
    auto sliderBg = hudPause.addElement<hud::Rect>(
        slider->position, slider->size, slider->color * .5f);
    auto logtxt = hudPause.addElement<hud::Text>(glm::vec2{-1.f, .3f});
    // hudPause.addElement<hud::Canvas>(glm::vec2{.3f, .3f}, glm::vec2{.2f,
    // .2f},
    //                                  glm::vec3{1.f, 1.f, 1.f});
    hudSys.setView(&hudWorld);

    // btn->addChild(std::make_shared<hud::Rect>(
    //     glm::vec2{.1f, .1f}, glm::vec2{.8f, .8f}, glm::vec3{1.f, 0.f, 0.f}));

    // std::vector<glm::vec3> lightColors{{1.f, .1f, .1f}, {.1f, .1f, 1.f},
    //                                    {.1f, 1.f, .1f}, {1.f, 1.f, .1f},
    //                                    {.1f, 1.f, 1.f}, {1.f, 1.f, 1.f}};
    //
    // for (int i = 0; i < lightColors.size(); i++) {
    //   auto rotateLight = glm::rotate(
    //       glm::mat4(1.f), (i * glm::two_pi<float>()) / lightColors.size(),
    //       {0.f, 1.f, 0.f});
    //   particleSys.particles.push_back(
    //       {.pos = rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f),
    //        .col = glm::vec4(lightColors[i], 1.f),
    //        .velocity = {},
    //        .timeOfDeath = static_cast<float>(glfwGetTime()) + 10.f});
    // }

    input::init(context);

    auto currentTime = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(context.window)) {
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();
      sliderBg->position = slider->position;
      sliderBg->size = slider->size;
      fpstxt->content =
          fmt::format("FPS: {}", static_cast<int>(1.f / frameTime));

      rect->size = fpstxt->size;
      orientationtxt->position.x = 1.f - orientationtxt->size.x;
      orientationtxt->content = fmt::format(
          "Orientation: {}", glm::to_string(context.camera.orientation));
      logtxt->content = fmt::format("{}", slider->value);
      currentTime = newTime;

      input::moveInPlaneXZ(context);

      audio::update(context);

      float aspect = context.window.aspectRatio;

      context.camera.projectionMatrix =
          glm::perspective(glm::radians(60.f), aspect, .1f, 1000.f);
      context.camera.projectionMatrix[1][1] *= -1; // Flip Y for Vulkan
      context.camera.projectionMatrix[0][0] *= -1; // Flip X for rotation
      camera::calcViewYXZ(context);

      if (auto commandBuffer = renderer::beginFrame(context)) {
        int frameIndex = renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
            context.vulkan.globalDescriptorSets[frameIndex],
        };

        // update
        GlobalUbo ubo{};
        ubo.projection = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.aspectRatio = aspect;
        particleSys.update();
        if (glfwGetKey(context.window, GLFW_KEY_U)) {
          skyParams.props.sunDir.x += .1f * context.frameInfo.dt;
          sky.update();
        }
        // waterSys.update(skyParams);
        context.vulkan.globalUBOs[frameIndex]->write(&ubo);
        context.vulkan.globalUBOs[frameIndex]->flush();

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        hudSys.render();
        if (hudSys.getView() == &hudWorld) {
          entitySys.render();
          // waterSys.render();

          particleSys.render();

          if (glm::length2(entities[1].transform.position -
                           context.camera.position) < 25.f)
            freezeAnimationSys.render();
        }

        renderer::endSwapChainRenderPass(commandBuffer);
        renderer::endFrame(context);
      }
    }
    vkDeviceWaitIdle(context.vulkan.device);
  }
  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
  audio::cleanup();
}
} // namespace vkh
