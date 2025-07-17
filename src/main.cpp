#include "vkh/audio.hpp"
#include "vkh/camera.hpp"
#include "vkh/cleanup.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/init.hpp"
#include "vkh/input.hpp"
#include "vkh/renderer.hpp"
#include "vkh/swapChain.hpp"
#include "vkh/systems/entity/entities.hpp"
#include "vkh/systems/freezeAnimation.hpp"
#include "vkh/systems/particles.hpp"
#include "vkh/systems/skybox.hpp"
#include "vkh/systems/water/water.hpp"
#include "vkh/window.hpp"

#include "vkh/systems/hud/elements/button.hpp"
#include "vkh/systems/hud/elements/canvas.hpp"
#include "vkh/systems/hud/elements/slider.hpp"
#include "vkh/systems/hud/hud.hpp"
#include <glm/geometric.hpp>

// #include <GameAnalytics/GameAnalytics.h>

std::mt19937 rng{std::random_device{}()};

#include "dungeonGenerator.hpp"

#include <print>

/*void setupAnalytics() {
  gameanalytics::GameAnalytics::setEnabledInfoLog(false);
  gameanalytics::GameAnalytics::setEnabledVerboseLog(false);

  gameanalytics::GameAnalytics::configureBuild("alpha");

  gameanalytics::GameAnalytics::configureAvailableResourceCurrencies(
      {"sanity"});
  gameanalytics::GameAnalytics::initialize(
      "99cf79ca1f6f9a87f9438ac39067640b",
      "7974f4f0c8f62b2f947aa5bf6a67aa81bbec784e");
}*/

void updateParticles(vkh::EngineContext &context,
                     vkh::ParticleSys &particleSys) {

  auto it = particleSys.particles.begin();
  while (it != particleSys.particles.end()) {
    float fakeMass = 5000.0f; // same as in emitter
    glm::vec3 direction = -it->pos;
    float distance = glm::length(direction);
    if (distance > 0.0001f) {
      constexpr float speed = .5f;
      glm::vec3 acceleration =
          (fakeMass / (distance * distance)) * glm::normalize(direction);
      it->velocity += acceleration * context.frameInfo.dt * speed;
      it->pos += it->velocity * context.frameInfo.dt * speed;
    }
    ++it;
  }

  particleSys.update();
}

void run() {
  vkh::EngineContext context{};
  vkh::initWindow(context);
  vkh::init(context);
  vkh::audio::init();
  vkh::renderer::init(context);
  { // {} to handle call destructors of buffers before vulkan is cleaned up
    std::vector<vkh::EntitySys::Entity> entities;
    vkh::EntitySys entitySys(context, entities);

    generateDungeon(context, entitySys);

    // vkh::ParticleSys particleSys(context);
    /*
    // Emitters
    for (int i = 0; i < 4000; i++) {
      std::uniform_real_distribution<float> rand;
      // Position within a sphere of radius ~10
      glm::vec3 pos =
          glm::normalize(glm::vec3(rand(rng) - 0.5f, rand(rng) - 0.5f,
                                   rand(rng) - 0.5f)) *
          (8.f + rand(rng) * 4.f); // 8 to 12 radius

      // Define radial and tangential direction
      glm::vec3 radial = glm::normalize(pos);
      glm::vec3 up = glm::vec3(0.f, 1.f, 0.f);
      if (glm::abs(glm::dot(radial, up)) > 0.95f)
        up = glm::vec3(1.f, 0.f, 0.f); // avoid colinearity

      glm::vec3 tangential = glm::normalize(glm::cross(up, radial));

      // Scaled gravity: use higher "fake mass" to speed up orbital motion at
      // larger radius
      float fakeMass = 5000.0f; // Tunable constant — tweak as needed
      float r = glm::length(pos);
      float speed = std::sqrt(fakeMass / r);

      glm::vec3 vel = tangential * speed;

      particleSys.particles.emplace_back(
          pos, glm::vec3{0.56f, 0.09f, 0.03f} + (rand(rng) - 0.5f) * 0.3f, vel);
    }*/
    vkh::FreezeAnimationSys freezeAnimationSys(context);
    vkh::SkyboxSys skyboxSys(context);
    // vkh::WaterSys waterSys(context, skyboxSys);
    vkh::HudSys hudSys(context);

    vkh::hud::View hudWorld(context);
    vkh::hud::View hudPause(context);
    vkh::hud::View canvasView(context);
    auto canvas = canvasView.addElement<vkh::hud::Canvas>(
        glm::vec2{-1.f, -1.f}, glm::vec2{2.f, 2.f},
        glm::vec3{.22f, .05f, .04f});
    auto rect = hudWorld.addElement<vkh::hud::Rect>(
        glm::vec2{-1.f, -1.f}, glm::vec2{.3f, .3f},
        glm::vec3{.678f, .007f, .388f});
    auto fpstxt = rect->addChild<vkh::hud::Text>(glm::vec2{0.f, 0.f});
    auto orientationtxt =
        hudWorld.addElement<vkh::hud::Text>(glm::vec2{1.f, -1.f});

    vkh::audio::Sound uiSound("sounds/ui.wav");
    vkh::audio::Sound paperSound("sounds/568962__efrane__ripping-paper-10.wav");
    auto go2Canvas = hudPause.addElement<vkh::hud::Button>(
        glm::vec2{.8f, -1.f}, glm::vec2{.2f, .2f}, glm::vec3{.5f, 0.f, .5f},
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            paperSound.play(AL_FALSE, AL_FALSE, dis(rng));
            hudSys.setView(&canvasView);
          }
        });
    glm::dvec2 worldCursorPos{};
    vkh::hud::EventListener<&vkh::EngineContext::InputCallbackSystem::key>
        worldKeyListener(hudWorld, [&](int key, int scancode, int action,
                                       int mods) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwGetCursorPos(context.window, &worldCursorPos.x,
                             &worldCursorPos.y);
            vkh::input::lastPos = worldCursorPos;
            glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            hudSys.setView(&hudPause);
          }
        });
    vkh::hud::EventListener<&vkh::EngineContext::InputCallbackSystem::key>
        pauseKeyListener(hudPause, [&](int key, int scancode, int action,
                                       int mods) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            vkh::input::lastPos = worldCursorPos;
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
    vkh::hud::EventListener<&vkh::EngineContext::InputCallbackSystem::key>
        canvasKeyListener(
            canvasView, [&](int key, int scancode, int action, int mods) {
              if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
                if (canvas->filePicker)
                  return;
                hudSys.setView(&hudPause);
              }
            });
    go2Canvas->addChild<vkh::hud::Text>(glm::vec2{}, "Go to canvas");
    hudSys.setView(&hudWorld);

    // btn->addChild(std::make_shared<vkh::hud::Rect>(
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
    //   particleSys.particles.emplace_back(
    //       {.pos = rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f),
    //        .col = glm::vec4(lightColors[i], 1.f),
    //        .velocity = {},
    //        .timeOfDeath = static_cast<float>(glfwGetTime()) + 10.f});
    // }

    vkh::input::init(context);

    context.camera.position = {0.f, 0.f, 0.f};
    context.camera.yaw = 1.5f * glm::pi<float>();

    auto currentTime = std::chrono::high_resolution_clock::now();
    auto initTime = currentTime;
    while (!glfwWindowShouldClose(context.window)) {
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();
      context.time = std::chrono::duration<float>(newTime - initTime).count();

      fpstxt->content =
          std::format("FPS: {}", static_cast<int>(1.f / frameTime));

      rect->size = fpstxt->size;
      orientationtxt->position.x = 1.f - orientationtxt->size.x;
      orientationtxt->content = std::format(
          "Yaw: {}\nPitch:{}", context.camera.yaw, context.camera.pitch);
      currentTime = newTime;

      vkh::input::update(context, entitySys);

      vkh::audio::update(context);

      context.camera.projectionMatrix =
          glm::perspective(1.919'862'177f /*human FOV*/,
                           context.window.aspectRatio, .1f, 1000.f);
      context.camera.projectionMatrix[1][1] *= -1; // Flip Y for Vulkan
      context.camera.projectionMatrix[0][0] *= -1; // Flip X for rotation
      vkh::camera::calcViewYXZ(context);

      if (auto commandBuffer = vkh::renderer::beginFrame(context)) {
        int frameIndex = vkh::renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
            context.vulkan.globalDescriptorSets[frameIndex],
        };

        // update
        vkh::GlobalUbo ubo{};
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = glfwGetTime();
        // updateParticles(context, particleSys);
        // waterSys.update();
        context.vulkan.globalUBOs[frameIndex]->write(&ubo);
        context.vulkan.globalUBOs[frameIndex]->flush();

        // render
        vkh::renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        if (hudSys.getView() == &hudWorld) {
          skyboxSys.render();
          // waterSys.render();
          entitySys.render();
          // particleSys.render();

          // if (glm::length2(entities[1].transform.position -
          //                  context.camera.position) < 25.f)
          //   freezeAnimationSys.render();
        }
        // if (glfwGetKey(context.window, GLFW_KEY_F1))
        hudSys.render();

        vkh::renderer::endSwapChainRenderPass(commandBuffer);
        vkh::renderer::endFrame(context);
      }
    }
    vkDeviceWaitIdle(context.vulkan.device);
  }
  vkh::renderer::cleanup(context);
  vkh::cleanup(context);
  cleanupWindow(context);
  vkh::audio::cleanup();
}
#if defined(WIN32) && defined(NDEBUG)
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine,
                   int nCmdShow) {
#else
int main() {
#endif
  // setupAnalytics();
  try {
    run();
  } catch (const std::exception &e) {
    std::println("{}", e.what());
    // gameanalytics::GameAnalytics::addErrorEvent(
    //     gameanalytics::EGAErrorSeverity::Critical, e.what());
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}
