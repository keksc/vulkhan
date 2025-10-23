#include <magic_enum/magic_enum.hpp>

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
#include "vkh/systems/hud/elements/button.hpp"
#include "vkh/systems/hud/elements/canvas.hpp"
#include "vkh/systems/hud/elements/rectImg.hpp"
#include "vkh/systems/hud/elements/text.hpp"
#include "vkh/systems/hud/hud.hpp"
#include "vkh/systems/particles.hpp"
#include "vkh/systems/skybox.hpp"
#include "vkh/systems/smoke/smoke.hpp"
#include "vkh/systems/water/water.hpp"
#include "vkh/window.hpp"

#include "dungeonGenerator.hpp"

std::mt19937 rng{std::random_device{}()};

void updateParticles(vkh::EngineContext &context,
                     vkh::ParticleSys &particleSys) {
  auto it = particleSys.particles.begin();
  while (it != particleSys.particles.end()) {
    float fakeMass = 5000.0f;
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

class KeybindEdit : public vkh::hud::Button {
public:
  KeybindEdit(vkh::hud::View &view, Element *parent, glm::vec2 position,
              glm::vec2 size, decltype(Button::imageIndex) imageIndex,
              std::function<void(int, int, int)> onClick,
              const std::string &label)
      : Button{view, parent, position, size, imageIndex, onClick, label} {}
  vkh::input::Action action;
};

void run() {
  vkh::EngineContext context{};
  vkh::initWindow(context);
  vkh::init(context);
  vkh::audio::init();
  vkh::input::init(context);

  vkh::renderer::init(context);

  {
    std::vector<vkh::EntitySys::Entity> entities;
    vkh::SkyboxSys skyboxSys(context);
    vkh::EntitySys entitySys(context, entities, skyboxSys);
    vkh::SmokeSys smokeSys(context);
    // vkh::WaterSys waterSys(context, skyboxSys);
    generateDungeon(context, entitySys);
    auto &cross = entitySys.entities.emplace_back(
        vkh::EntitySys::Transform{.position = {}}, vkh::EntitySys::RigidBody{},
        std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
            context, "models/cross.glb"));
    auto piano = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
        context, "models/piano-decent.glb");
    for (int i = 0; i < piano->meshes.size(); i++)
      entitySys.entities.emplace_back(
          vkh::EntitySys::Transform{.position = {10.f, 10.f, 10.f}},
          vkh::EntitySys::RigidBody{}, piano, i);

    vkh::FreezeAnimationSys freezeAnimationSys(context);
    vkh::HudSys hudSys(context);

    vkh::hud::View canvasView(context, hudSys);
    vkh::hud::View settingsView(context, hudSys);
    vkh::hud::View hudWorld(context, hudSys);
    vkh::hud::View hudPause(context, hudSys);
    vkh::hud::View hudSmoke(context, hudSys);

    auto canvas = canvasView.addElement<vkh::hud::Canvas>(
        glm::vec2{-1.f, -1.f}, glm::vec2{2.f, 2.f}, 0);

    vkh::audio::Sound uiSound("sounds/ui.wav");
    vkh::audio::Sound paperSound("sounds/568962__efrane__ripping-paper-10.wav");
    auto canvasBtn = hudPause.addElement<vkh::hud::Button>(
        glm::vec2{.8f, -1.f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            paperSound.play(AL_FALSE, AL_FALSE, dis(rng));
            hudSys.setView(&canvasView);
          }
        },
        "Go to canvas");
    auto smokeBtn = hudPause.addElement<vkh::hud::Button>(
        glm::vec2{.8f, .8f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            hudSys.setView(&hudSmoke);
          }
        },
        "Go to smoke");
    auto smokeViewEventManager =
        hudSmoke.addElement<vkh::hud::Element>(glm::vec2{0.0}, glm::vec2{1.0});
    smokeViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (action != GLFW_PRESS)
            return false;
          if (key == GLFW_KEY_ESCAPE) {
            hudSys.setView(&hudPause);
            return true;
          }
          return false;
        });
    auto settingsBtn = hudPause.addElement<vkh::hud::Button>(
        glm::vec2{-1.f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            paperSound.play(AL_FALSE, AL_FALSE, dis(rng));
            hudSys.setView(&settingsView);
          }
        },
        "Edit settings");
    settingsView.addElement<vkh::hud::Text>(glm::vec2{-1.f}, "Keybinds");

    std::shared_ptr<KeybindEdit> selectedButton;

    using namespace std::string_literals;

    glm::dvec2 worldCursorPos{};
    glm::vec2 worldYawAndPitch{};
    auto settingsViewEventManager = settingsView.addElement<vkh::hud::Element>(
        glm::vec2{0.0}, glm::vec2{1.0});
    settingsViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (action != GLFW_PRESS)
            return false;
          if (key == GLFW_KEY_ESCAPE) {
            hudSys.setView(&hudPause);
            return true;
          }
          if (selectedButton) {
            selectedButton->label->content =
                static_cast<std::string>(
                    magic_enum::enum_name(selectedButton->action)) +
                ":" + glfwGetKeyName(key, 0);
            vkh::input::keybinds[selectedButton->action] = key;
            selectedButton = nullptr;
          }
          return false;
        });
    auto pauseViewEventManager =
        hudPause.addElement<vkh::hud::Element>(glm::vec2{0.0}, glm::vec2{1.0});
    pauseViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(action == GLFW_PRESS && key == GLFW_KEY_ESCAPE))
            return false;
          vkh::input::lastPos = worldCursorPos;
          glfwSetCursorPos(context.window, worldCursorPos.x, worldCursorPos.y);
          context.camera.yaw = worldYawAndPitch.x;
          context.camera.pitch = worldYawAndPitch.y;
          glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          hudSys.setView(&hudWorld);
          return true;
        });

    unsigned short i = 0;
    for (auto &[action, bind] : vkh::input::keybinds) {
      auto btn = settingsView.addElement<KeybindEdit>(
          glm::vec2{0.f, -1.f + .2f * i}, glm::vec2{.2f}, 0,
          [&](int button, int action, int) {},
          static_cast<std::string>(magic_enum::enum_name(action)) + ":" +
              glfwGetKeyName(bind, 0));
      btn->setCallback([&, btn](int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
          static std::uniform_real_distribution<float> dis(.7f, 1.3f);
          paperSound.play(AL_FALSE, AL_FALSE, dis(rng));
          selectedButton = btn;
        }
      });
      btn->action = action;
      i++;
    }

    auto worldViewEventManager =
        hudWorld.addElement<vkh::hud::Element>(glm::vec2{0.0}, glm::vec2{1.0});
    worldViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS))
            return false;
          glfwGetCursorPos(context.window, &worldCursorPos.x,
                           &worldCursorPos.y);
          vkh::input::lastPos = worldCursorPos;
          worldYawAndPitch = {context.camera.yaw, context.camera.pitch};
          glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
          hudSys.setView(&hudPause);
          return true;
        });

    auto canvasViewEventManager = canvasView.addElement<vkh::hud::Element>(
        glm::vec2{0.0}, glm::vec2{1.0});
    canvasViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS))
            return false;
          hudSys.setView(&hudPause);
          return true;
        });
    hudSys.setView(&hudWorld);

    auto rect = hudWorld.addElement<vkh::hud::Rect>(glm::vec2{-1.f, -1.f},
                                                    glm::vec2{.3f, .3f}, 0);
    auto fpstxt = rect->addChild<vkh::hud::Text>(glm::vec2{0.f, 0.f});
    auto orientationtxt =
        hudWorld.addElement<vkh::hud::Text>(glm::vec2{1.f, -1.f});

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

      cross.transform.position.y = .7f + .2f * sin(glfwGetTime());

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

        vkh::GlobalUbo ubo{};
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.resolution = context.window.size;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = glfwGetTime();
        context.vulkan.globalUBOs[frameIndex]->write(&ubo);
        context.vulkan.globalUBOs[frameIndex]->flush();

        // waterSys.update();
        smokeSys.update();

        vkh::renderer::beginSwapChainRenderPass(context, commandBuffer);

        if (hudSys.getView() == &hudWorld) {
          // waterSys.render();
          skyboxSys.render();
          entitySys.render();
        }
        if (hudSys.getView() == &hudSmoke) {
          smokeSys.render();
        }
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
  try {
    run();
  } catch (const std::exception &e) {
    std::println("{}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
