#include <GLFW/glfw3.h>
#include <chrono>
#include <magic_enum/magic_enum.hpp>

#include "bosses/featherDuckGuard.hpp"
#include "vkh/audio.hpp"
#include "vkh/camera.hpp"
#include "vkh/cleanup.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/exepath.hpp"
#include "vkh/init.hpp"
#include "vkh/input.hpp"
#include "vkh/renderer.hpp"
#include "vkh/swapChain.hpp"
#include "vkh/systems/entity/entities.hpp"
#include "vkh/systems/hud/hud.hpp"
#include "vkh/systems/particles.hpp"
#include "vkh/systems/skybox.hpp"
#include "vkh/systems/smoke/smoke.hpp"
#include "vkh/systems/water/water.hpp"
#include "vkh/window.hpp"

#include "vkh/systems/hud/elements/button.hpp"
#include "vkh/systems/hud/elements/canvas.hpp"
#include "vkh/systems/hud/elements/rectImg.hpp"
#include "vkh/systems/hud/elements/text.hpp"
#include "vkh/systems/hud/elements/textInput.hpp"

#include "../server/packet.hpp"
#include "dungeonGenerator.hpp"
#include "network.hpp"

#include <random>
#include <string>

std::mt19937 rng{std::random_device{}()};

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
  execpath::setWorkingDirectoryToExecutable();

  vkh::renderer::init(context);

  {
    std::chrono::time_point<std::chrono::high_resolution_clock> audioFadeBegin;
    const float audioFadeSpeed = 2.5f;

    // vkh::audio::Sound boringSpeech("sounds/Rhorhorho.opus");
    // boringSpeech.play();
    vkh::audio::Sound bgm("sounds/Enter Remollon.opus");
    bgm.play();

    vkh::SkyboxSys skyboxSys(context);
    vkh::EntitySys entitySys(context);
    vkh::SmokeSys smokeSys(context);
    // vkh::WaterSys waterSys(context, skyboxSys);
    vkh::ParticleSys particleSys(context);

    auto &entities = entitySys.entities;
    generateDungeon(context, entitySys, entities);
    auto piano = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
        context, "models/piano-decent.glb", entitySys.textureSetLayout);
    for (size_t i = 0; i < piano->meshes.size(); i++)
      entities.emplace_back(
          vkh::EntitySys::Transform{.position{10.f, 10.f, 10.f}},
          vkh::EntitySys::RigidBody{}, piano, i);

    auto manorcore = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
        context, "models/manorcore.glb", entitySys.textureSetLayout);
    for (size_t i = 0; i < manorcore->meshes.size(); i++)
      entities.emplace_back(
          vkh::EntitySys::Transform{.position{25.f}, .scale{1.f}},
          vkh::EntitySys::RigidBody{}, manorcore, i);
    // waterSys.downloadDisplacementAtWorldPos();

    auto shoe = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
        context, "models/MaterialsVariantsShoe.glb",
        entitySys.textureSetLayout);
    auto playerModel = shoe;

    std::unordered_map<uint32_t, uint32_t> playersIndices;

    // Sort to group meshes for indirect drawing
    std::sort(
        entities.begin(), entities.end(),
        [](const vkh::EntitySys::Entity &a, const vkh::EntitySys::Entity &b) {
          if (a.scene != b.scene)
            return a.scene < b.scene;
          return a.meshIndex < b.meshIndex;
        });

    entitySys.updateBuffers();

    vkh::HudSys hudSys(context);

    vkh::hud::View worldView(context, hudSys);
    vkh::hud::View pauseView(context, hudSys);
    vkh::hud::View settingsView(context, hudSys);
    vkh::hud::View canvasView(context, hudSys);
    vkh::hud::View smokeView(context, hudSys);

    FeatherDuckGuard featherDuckGuard(context, entitySys, worldView);

    auto canvas = canvasView.addElement<vkh::hud::Canvas>(
        glm::vec2{-1.f, -1.f}, glm::vec2{2.f, 2.f}, 0);

    // vkh::audio::Sound uiSound("sounds/ui.wav");
    // vkh::audio::Sound
    // paperSound("sounds/568962__efrane__ripping-paper-10.wav");
    auto canvasBtn = pauseView.addElement<vkh::hud::Button>(
        glm::vec2{.8f, -1.f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            // static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            // paperSound.setPitch(dis(rng));
            // paperSound.play();
            hudSys.setView(&canvasView);
          }
        },
        "Go to canvas");
    auto smokeBtn = pauseView.addElement<vkh::hud::Button>(
        glm::vec2{.8f, .8f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            hudSys.setView(&smokeView);
          }
        },
        "Go to smoke");
    auto smokeViewEventManager =
        smokeView.addElement<vkh::hud::Element>(glm::vec2{0.0}, glm::vec2{1.0});
    smokeViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (action != GLFW_PRESS)
            return false;
          if (key == GLFW_KEY_ESCAPE) {
            hudSys.setView(&pauseView);
            return true;
          }
          return false;
        });
    auto settingsBtn = pauseView.addElement<vkh::hud::Button>(
        glm::vec2{-1.f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            // static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            // paperSound.setPitch(dis(rng));
            // paperSound.play();
            hudSys.setView(&settingsView);
          }
        },
        "Edit settings");
    settingsView.addElement<vkh::hud::Text>(glm::vec2{-1.f}, "Keybinds");

    std::unique_ptr<Network> network;
    auto addr = pauseView.addElement<vkh::hud::TextInput>(glm::vec2{-1.f, 0.f},
                                                          "server address");
    pauseView.addElement<vkh::hud::Button>(
        glm::vec2{-1.f, 0.2f}, glm::vec2{.2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
            try {
              network = std::make_unique<Network>(addr->getContent().c_str());
            } catch (const std::exception &e) {
              std::println("{}", e.what());
            }
        },
        "Connect");
    pauseView.addElement<vkh::hud::Button>(
        glm::vec2{-.8f, 0.2f}, glm::vec2{.2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
            network = nullptr;
        },
        "Disonnect");

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
            hudSys.setView(&pauseView);
            return true;
          }
          if (selectedButton) {
            selectedButton->label->content =
                static_cast<std::string>(
                    magic_enum::enum_name(selectedButton->action)) +
                ":" + vkh::input::getKeyName(key);
            vkh::input::keybinds[selectedButton->action] = key;
            selectedButton = nullptr;
          }
          return false;
        });
    auto pauseViewEventManager =
        pauseView.addElement<vkh::hud::Element>(glm::vec2{0.0}, glm::vec2{1.0});
    pauseViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(action == GLFW_PRESS && key == GLFW_KEY_ESCAPE))
            return false;
          vkh::input::lastPos = worldCursorPos;
          glfwSetCursorPos(context.window, worldCursorPos.x, worldCursorPos.y);
          context.camera.yaw = worldYawAndPitch.x;
          context.camera.pitch = worldYawAndPitch.y;
          glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
          hudSys.setView(&worldView);
          return true;
        });

    unsigned short i = 0;
    for (auto &[action, bind] : vkh::input::keybinds) {
      auto kbEdit = settingsView.addElement<KeybindEdit>(
          glm::vec2{0.f, -1.f + .2f * i}, glm::vec2{.2f}, 0,
          [&](int button, int action, int) {},
          std::string(magic_enum::enum_name(action)) + ":" +
              vkh::input::getKeyName(bind));
      kbEdit->setCallback([&, kbEdit](int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
          // static std::uniform_real_distribution<float> dis(.7f, 1.3f);
          // paperSound.setPitch(dis(rng));
          // paperSound.play();
          selectedButton = kbEdit;
        }
      });
      kbEdit->action = action;
      i++;
    }

    auto worldViewEventManager =
        worldView.addElement<vkh::hud::Element>(glm::vec2{0.0}, glm::vec2{1.0});
    worldViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwGetCursorPos(context.window, &worldCursorPos.x,
                             &worldCursorPos.y);
            vkh::input::lastPos = worldCursorPos;
            worldYawAndPitch = {context.camera.yaw, context.camera.pitch};
            glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            hudSys.setView(&pauseView);
            return true;
          }
          return false;
        });
    worldViewEventManager->addEventHandler<vkh::input::EventType::WindowFocus>(
        [&](int focused) {
          audioFadeBegin = std::chrono::high_resolution_clock::now();
          return false;
        });

    auto canvasViewEventManager = canvasView.addElement<vkh::hud::Element>(
        glm::vec2{0.0}, glm::vec2{1.0});
    canvasViewEventManager->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS))
            return false;
          hudSys.setView(&pauseView);
          return true;
        });
    hudSys.setView(&worldView);

    auto rect = worldView.addElement<vkh::hud::Rect>(glm::vec2{-1.f, -1.f},
                                                     glm::vec2{.3f, .3f}, 0);
    auto fpstxt = rect->addChild<vkh::hud::Text>(glm::vec2{0.f, 0.f});
    auto orientationtxt =
        worldView.addElement<vkh::hud::Text>(glm::vec2{1.f, -1.f});

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

      featherDuckGuard.update();

      float deltaTime =
          std::chrono::duration<float>(newTime - audioFadeBegin).count();
      float volume;
      if (context.window.isFocused()) {
        volume = glm::clamp(deltaTime * audioFadeSpeed, 0.f, 1.f);
      } else {
        volume = glm::clamp(1.f - deltaTime * audioFadeSpeed, 0.f, 1.f);
      }
      vkh::audio::setVolume(volume);

      if (network) {
        bool needUpdate = false;

        std::vector<uint8_t> pktData;
        while (network->receive(pktData)) {
          if (pktData.size() >= sizeof(Packet)) {
            needUpdate = true;
            Packet *p = reinterpret_cast<Packet *>(pktData.data());

            if (p->type == PacketType::Join) {
              playersIndices[p->id] = entities.size();
              for (size_t i = 0; i < playerModel->meshes.size(); i++)
                entities.emplace_back(vkh::EntitySys::Transform{.position{5.f}},
                                      vkh::EntitySys::RigidBody{}, playerModel,
                                      i, p->id);
            } else if (p->type == PacketType::Leave) {
              for (int i = 0; i < entities.size();) {
                if (entities[i].id == p->id) {
                  entities[i] = entities.back();
                  entities.pop_back();
                } else {
                  ++i;
                }
              }
            } else if (p->type == PacketType::Update &&
                       pktData.size() >= sizeof(UpdatePacket)) {
              UpdatePacket *up =
                  reinterpret_cast<UpdatePacket *>(pktData.data());
              if (playersIndices.contains(up->id)) {
                auto begin = entities.begin() + playersIndices[up->id];
                for (size_t i = 0; i < playerModel->meshes.size(); i++) {
                  (begin + i)->transform.position = up->position;
                  (begin + i)->transform.orientation = up->orientation;
                }
              }
            }
          }
        }

        // Send local player position to the server
        UpdatePacket myUpdate;
        myUpdate.type = PacketType::Update;
        myUpdate.id = 0; // Overwritten by server
        myUpdate.position = context.camera.position;
        myUpdate.orientation = context.camera.orientation;

        network->send(&myUpdate, sizeof(myUpdate));
        if (needUpdate)
          entitySys.updateBuffers();
      }

      static bool dontDoOnce = true;
      if (!dontDoOnce) {
        if (!context.window.isFocused())
          continue;
      }
      dontDoOnce = false;

      fpstxt->content =
          std::format("FPS: {}", static_cast<int>(1.f / frameTime));

      rect->size = fpstxt->size;
      orientationtxt->position.x = 1.f - orientationtxt->size.x;
      orientationtxt->content = std::format(
          "Yaw: {}\nPitch:{}", context.camera.yaw, context.camera.pitch);

      currentTime = newTime;

      vkh::input::update(context, entities);

      vkh::audio::update(context);

      context.camera.projectionMatrix =
          glm::perspective(1.919'862'177f /*human FOV*/,
                           context.window.aspectRatio, 1000.f, .01f);
      context.camera.projectionMatrix[1][1] *= -1.f; // Flip Y
      vkh::camera::calcViewYXZ(context);

      if (auto commandBuffer = vkh::renderer::beginFrame(context)) {
        int frameIndex = vkh::renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
        };

        vkh::GlobalUbo ubo{};
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.resolution = context.window.size;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = glfwGetTime();
        context.vulkan.globalUBOs[frameIndex].write(&ubo);
        context.vulkan.globalUBOs[frameIndex].flush();

        if (hudSys.getView() == &worldView) {
          // waterSys.update();
          particleSys.update();

          entitySys.updateJoints();
        }
        if (hudSys.getView() == &smokeView) {
          smokeSys.update();
        }

        vkh::renderer::beginSwapChainRenderPass(context, commandBuffer);

        // MSAA subpass
        if (hudSys.getView() == &worldView) {
          skyboxSys.render();
          entitySys.render();
          // waterSys.render();
        }

        vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

        // 1x subpass
        if (hudSys.getView() == &worldView) {
          particleSys.render();
        }
        if (hudSys.getView() == &smokeView) {
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
