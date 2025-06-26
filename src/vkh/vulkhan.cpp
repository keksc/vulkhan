#include "vulkhan.hpp"
#include "systems/hud/hudElements.hpp"
#include "systems/skybox.hpp"
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

#include "audio.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "cleanupVulkan.hpp"
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
#include <vector>

namespace vkh {
void run() {
  EngineContext context{};
  initWindow(context);
  initVulkan(context);
  audio::init();
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkan is cleaned up
    std::vector<EntitySys::Entity> entities;
    EntitySys entitySys(context, entities);

    auto manorcore = entitySys.createScene("models/manorcore.glb");
    entitySys.entities.push_back(
        {{.position = {}, .scale = glm::vec3(10.f)}, {}, manorcore});
    // auto env = entitySys.createScene("models/env.glb");
    // entitySys.entities.push_back(
    //     {{.position = {0.f, -25.f, 0.f}, .scale = glm::vec3(30.f)}, {}, env});
    // generateDungeon(entitySys);

    ParticleSys particleSys(context);
    FreezeAnimationSys freezeAnimationSys(context);
    SkyboxSys skyboxSys(context);
    WaterSys waterSys(context, skyboxSys);
    HudSys hudSys(context);

    hud::View hudWorld(context);
    hud::View hudPause(context);
    hud::View canvasView(context);
    auto canvas = canvasView.addElement<hud::Canvas>(
        glm::vec2{-1.f, -1.f}, glm::vec2{2.f, 2.f},
        glm::vec3{.22f, .05f, .04f});
    canvas->lineColor = {.72f, .69f, .68f};
    auto rSlider = canvas->addChild<hud::Slider>(
        glm::vec2{}, glm::vec2{.5f}, glm::vec3{}, glm::vec3{.3f},
        glm::vec2{0.f, 1.f}, canvas->lineColor.r);
    auto gSlider = canvas->addChild<hud::Slider>(
        glm::vec2{0.f, .5f}, glm::vec2{.5f}, glm::vec3{}, glm::vec3{.3f},
        glm::vec2{0.f, 1.f}, canvas->lineColor.g);
    auto bSlider = canvas->addChild<hud::Slider>(
        glm::vec2{0.f, 1.f}, glm::vec2{.5f}, glm::vec3{}, glm::vec3{.3f},
        glm::vec2{0.f, 1.f},
        canvas->lineColor.b); // TODO: make these bitches display on the canvas
    auto rect = hudWorld.addElement<hud::Rect>(glm::vec2{-1.f, -1.f},
                                               glm::vec2{.3f, .3f},
                                               glm::vec3{.678f, .007f, .388f});
    auto fpstxt = rect->addChild<hud::Text>(glm::vec2{0.f, 0.f});
    auto orientationtxt = hudWorld.addElement<hud::Text>(glm::vec2{1.f, -1.f});

    audio::Sound uiSound("sounds/ui.wav");
    audio::Sound paperSound("sounds/568962__efrane__ripping-paper-10.wav");
    auto go2Canvas = hudPause.addElement<hud::Button>(
        glm::vec2{.8f, -1.f}, glm::vec2{.2f, .2f}, glm::vec3{0.f, 0.f, .5f},
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            paperSound.play();
            hudSys.setView(&canvasView);
          }
        });
    glm::dvec2 worldCursorPos{};
    hud::EventListener<&EngineContext::InputCallbackSystem::key>
        worldKeyListener(hudWorld, [&](int key, int scancode, int action,
                                       int mods) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwGetCursorPos(context.window, &worldCursorPos.x,
                             &worldCursorPos.y);
            input::lastPos = worldCursorPos;
            glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            hudSys.setView(&hudPause);
          }
        });
    hud::EventListener<&EngineContext::InputCallbackSystem::key>
        pauseKeyListener(hudPause, [&](int key, int scancode, int action,
                                       int mods) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            input::lastPos = worldCursorPos;
            glfwSetCursorPos(context.window, worldCursorPos.x,
                             worldCursorPos.y);
            glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            hudSys.setView(&hudWorld);
          }
        });
    // if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
    //   return;
    // }
    // if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    //   glfwSetWindowShouldClose(context->window, GLFW_TRUE);
    // }
    hud::EventListener<&EngineContext::InputCallbackSystem::key>
        canvasKeyListener(canvasView,
                          [&](int key, int scancode, int action, int mods) {
                            if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS)
                              hudSys.setView(&hudPause);
                          });
    go2Canvas->addChild<hud::Text>(glm::vec2{}, "Go to canvas");
    hudSys.setView(&hudWorld);

    // btn->addChild(std::make_shared<hud::Rect>(
    //     glm::vec2{.1f, .1f}, glm::vec2{.8f, .8f}, glm::vec3{1.f, 0.f,
    //     0.f}));

    // std::vector<glm::vec3> lightColors{{1.f, .1f, .1f}, {.1f, .1f, 1.f},
    //                                    {.1f, 1.f, .1f}, {1.f, 1.f, .1f},
    //                                    {.1f, 1.f, 1.f}, {1.f, 1.f, 1.f}};
    //
    // for (int i = 0; i < lightColors.size(); i++) {
    //   auto rotateLight = glm::rotate(
    //       glm::mat4(1.f), (i * glm::two_pi<float>()) /
    //       lightColors.size(), {0.f, 1.f, 0.f});
    //   particleSys.particles.push_back(
    //       {.pos = rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f),
    //        .col = glm::vec4(lightColors[i], 1.f),
    //        .velocity = {},
    //        .timeOfDeath = static_cast<float>(glfwGetTime()) + 10.f});
    // }

    input::init(context);

    context.camera.position = {0.f, 1.f, -2.5f};
    context.camera.yaw = 1.5f * glm::pi<float>();

    auto currentTime = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(context.window)) {
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();

      canvas->lineColor.r = rSlider->value;
      canvas->lineColor.g = gSlider->value;
      canvas->lineColor.b = bSlider->value;

      fpstxt->content =
          fmt::format("FPS: {}", static_cast<int>(1.f / frameTime));

      rect->size = fpstxt->size;
      orientationtxt->position.x = 1.f - orientationtxt->size.x;
      orientationtxt->content = fmt::format(
          "Yaw: {}\nPitch:{}", context.camera.yaw, context.camera.pitch);
      currentTime = newTime;

      input::moveInPlaneXZ(context);

      audio::update(context);

      context.camera.projectionMatrix = glm::perspective(
          1.919'862'177f /*220deg*/, context.window.aspectRatio, .1f, 1000.f);
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
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = glfwGetTime();
        particleSys.update();
        waterSys.update();
        context.vulkan.globalUBOs[frameIndex]->write(&ubo);
        context.vulkan.globalUBOs[frameIndex]->flush();

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        if (hudSys.getView() == &hudWorld) {
          skyboxSys.render();
          waterSys.render();
          entitySys.render();
          particleSys.render();

          // if (glm::length2(entities[1].transform.position -
          //                  context.camera.position) < 25.f)
          //   freezeAnimationSys.render();
        }
        // if (glfwGetKey(context.window, GLFW_KEY_F1))
        hudSys.render();

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
