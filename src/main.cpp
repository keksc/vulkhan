#include <GLFW/glfw3.h>
#include <glm/gtx/string_cast.hpp>
#include <magic_enum/magic_enum.hpp>

// #include "entities/bosses/featherDuckGuard.hpp"
#include "modding.hpp"
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
#include "vkh/systems/hud/view.hpp"
#include "vkh/systems/particles.hpp"
#include "vkh/systems/skybox.hpp"
#include "vkh/systems/smoke/smoke.hpp"
#include "vkh/systems/water/water.hpp"
#include "vkh/window.hpp"

#include "UI/bindEdit.hpp"
#include "UI/button.hpp"
#include "UI/canvas.hpp"
#include "UI/rectImg.hpp"
#include "UI/scrollable.hpp"
#include "UI/stylizedBtn.hpp"
#include "UI/text.hpp"
#include "UI/textInput.hpp"

#include "../server/packet.hpp"
#include "dungeonGenerator.hpp"
#include "network.hpp"

#include <chrono>
#include <random>
#include <ranges>
#include <string>

std::mt19937 rng{std::random_device{}()};

std::vector<glm::mat4> genTransform() {

  std::uniform_int_distribution<uint32_t> countDist(
      3, 5); // 3 to 5 transformations looks great in 3D
  uint32_t numTransforms = countDist(rng);

  // Keep scales strictly bound so the 3D volume remains stable and
  // contained
  std::uniform_real_distribution<float> scaleDist(0.35f, 0.55f);
  std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
  std::uniform_real_distribution<float> transDist(
      -0.6f, 0.6f); // 3D translation limits

  std::vector<glm::mat4> transformationMats;
  transformationMats.reserve(numTransforms);

  for (uint32_t i = 0; i < numTransforms; ++i) {
    float sX = scaleDist(rng);
    float sY = scaleDist(rng);
    float sZ = scaleDist(rng); // Add Z scaling

    // Random angles for all 3 axes
    float ax = angleDist(rng);
    float ay = angleDist(rng);
    float az = angleDist(rng);

    // Create distinct rotation matrices for each axis
    glm::mat4 rotX =
        glm::rotate(glm::mat4(1.0f), ax, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rotY =
        glm::rotate(glm::mat4(1.0f), ay, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotZ =
        glm::rotate(glm::mat4(1.0f), az, glm::vec3(0.0f, 0.0f, 1.0f));

    // Combine rotations: R = Z * Y * X
    glm::mat4 combinedRotation = rotZ * rotY * rotX;

    // Apply scaling directly to the directional basis vectors
    // (columns 0, 1, 2)
    combinedRotation[0] = combinedRotation[0] * sX;
    combinedRotation[1] = combinedRotation[1] * sY;
    combinedRotation[2] = combinedRotation[2] * sZ;

    // Translation
    combinedRotation[3][0] = transDist(rng);
    combinedRotation[3][1] = transDist(rng);
    combinedRotation[3][2] = transDist(rng);

    combinedRotation[3][3] = 1.0f;

    transformationMats.push_back(combinedRotation);
  }
  return transformationMats;
}

void run() {
  vkh::EngineContext context{};
  vkh::initWindow(context);
  vkh::init(context);
  vkh::audio::init();
  vkh::input::init(context);
  execpath::setWorkingDirectoryToExecutable();

  vkh::renderer::init(context);

  {
    ModManager modMgr;

    std::chrono::time_point<std::chrono::high_resolution_clock> audioFadeBegin;
    const float audioFadeSpeed = 2.5f;

    // vkh::audio::Sound boringSpeech("sounds/Rhorhorho.opus");
    // boringSpeech.play();
    // vkh::audio::Sound bgm("sounds/Enter Remollon.opus");
    // bgm.play();

    vkh::SkyboxSys skyboxSys(context);
    vkh::EntitySys entitySys(context);
    vkh::SmokeSys smokeSys(context);
    // vkh::WaterSys waterSys(context, skyboxSys);
    vkh::ParticleSys particleSys(context);

    auto &entities = entitySys.entities;
    generateDungeon(context, entitySys);
    // auto piano = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/piano-decent.glb", entitySys.texturesSetLayout);
    // for (size_t i = 0; i < piano->meshes.size(); i++)
    //   entities.emplace_back(
    //       vkh::EntitySys::Transform{.position{10.f, 10.f, 10.f}},
    //       vkh::EntitySys::RigidBody{}, piano, i);
    //
    // auto manorcore = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/manorcore.glb", entitySys.texturesSetLayout);
    // for (size_t i = 0; i < manorcore->meshes.size(); i++)
    //   entities.emplace_back(
    //       vkh::EntitySys::Transform{.position{25.f}, .scale{1.f}},
    //       vkh::EntitySys::RigidBody{}, manorcore, i);
    // // waterSys.downloadDisplacementAtWorldPos();
    //
    // auto shoe = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/MaterialsVariantsShoe.glb",
    //     entitySys.texturesSetLayout);
    // auto playerModel = shoe;
    //
    // std::unordered_map<uint32_t, uint32_t> playersIndices;
    //
    // // Sort to group meshes for indirect drawing
    // std::sort(
    //     entities.begin(), entities.end(),
    //     [](const vkh::EntitySys::Entity &a, const vkh::EntitySys::Entity &b)
    //     {
    //       if (a.scene != b.scene)
    //         return a.scene < b.scene;
    //       return a.meshIndex < b.meshIndex;
    //     });
    //
    // entitySys.updateBuffers();

    vkh::HudSys hudSys(context);
    hudSys.solidColorSys.addTextureFromFile(
        "textures/hud.png"); // Will be default texture since at index 0

    vkh::hud::View worldView(context, hudSys);
    vkh::hud::View pauseView(context, hudSys);
    vkh::hud::View settingsView(context, hudSys);
    vkh::hud::View canvasView(context, hudSys);
    vkh::hud::View smokeView(context, hudSys);

    // std::vector<vkh::hud::Polygon::Vertex> vertices;
    //
    // const size_t n = 5;
    // const float R = .3f;
    // const float r = .2f;
    // glm::vec2 c{.5f};
    // for (int i = 0; i < 2 * n; i++) {
    //   float angle = i * M_PI / n; // 2π / (2n) = π / n
    //   float radius = (i % 2 == 0) ? R : r;
    //
    //   vkh::hud::Polygon::Vertex v;
    //   v.pos.x = c.x + radius * cos(angle);
    //   v.pos.y = c.y + radius * sin(angle);
    //
    //   vertices.emplace_back(v);
    // }
    // worldView.container.addChild<vkh::hud::Polygon>(vertices, 0);

    // FeatherDuckGuard featherDuckGuard(context, entitySys, worldView);

    auto canvas = canvasView.container.addChild<UI::Canvas>(glm::vec2{},
                                                            glm::vec2{1.f}, 0);

    // vkh::audio::Sound uiSound("sounds/ui.wav");
    // vkh::audio::Sound
    // paperSound("sounds/568962__efrane__ripping-paper-10.wav");
    auto canvasBtn = pauseView.container.addChild<UI::StylizedBtn>(
        glm::vec2{.8f, 0.f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            // static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            // paperSound.setPitch(dis(rng));
            // paperSound.play();
            hudSys.setView(&canvasView);
          }
        },
        "Go to canvas");
    auto smokeBtn = pauseView.container.addChild<UI::Button>(
        glm::vec2{.8f, .8f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            hudSys.setView(&smokeView);
          }
        },
        "Go to smoke");
    smokeView.container.addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (action != GLFW_PRESS)
            return false;
          if (key == GLFW_KEY_ESCAPE) {
            hudSys.setView(&pauseView);
            return true;
          }
          return false;
        });
    auto settingsBtn = pauseView.container.addChild<UI::Button>(
        glm::vec2{}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            // static std::uniform_real_distribution<float> dis(.7f, 1.3f);
            // paperSound.setPitch(dis(rng));
            // paperSound.play();
            hudSys.setView(&settingsView);
          }
        },
        "Edit settings");

    glm::vec2 keyBtnSize{.1f};

    auto settingsScroll = settingsView.container.addChild<UI::Scrollable>(
        glm::vec2{}, glm::vec2{1.f}, glm::vec2{0.f, .25f}, glm::vec2{0.f},
        glm::vec2{-1.f + static_cast<float>(vkh::input::keybinds.size()) *
                             keyBtnSize});
    settingsScroll->addChild<UI::Text>(glm::vec2{}, "Keybinds");

    std::shared_ptr<UI::BindEdit> selectedButton;

    unsigned short i = 0;
    for (auto &[action, bind] : vkh::input::keybinds | std::views::reverse) {
      auto kbEdit = settingsScroll->addChild<UI::BindEdit>(
          glm::vec2{0.5f, 0.f + .1f * i}, keyBtnSize, 0,
          [&](int button, int action, int) {},
          std::string(magic_enum::enum_name(action)) + ":" +
              vkh::input::getKeyName(bind));
      kbEdit->setCallback([&, kbEdit](int button, int action, int) {
        if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
          selectedButton = kbEdit;
        }
      });
      kbEdit->action = action;
      // auto mouseEdit = settingsScroll->addChild<UI::BindEdit>(
      //     glm::vec2{0.5f, 0.f + .1f * i}, keyBtnSize, 0,
      //     [&](int button, int action, int) {},
      //     std::string(magic_enum::enum_name(action)) + ":" +
      //         vkh::input::getKeyName(bind));
      i++;
    }

    settingsScroll->addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (action != GLFW_PRESS)
            return false;
          if (selectedButton) {
            if (key == GLFW_KEY_ESCAPE) {
              selectedButton->label->content =
                  static_cast<std::string>(
                      magic_enum::enum_name(selectedButton->action)) +
                  ":" + vkh::input::getKeyName(GLFW_KEY_UNKNOWN);
              vkh::input::keybinds[selectedButton->action] = GLFW_KEY_UNKNOWN;
              selectedButton = nullptr;
              return true;
            }
            selectedButton->label->content =
                static_cast<std::string>(
                    magic_enum::enum_name(selectedButton->action)) +
                ":" + vkh::input::getKeyName(key);
            vkh::input::keybinds[selectedButton->action] = key;
            selectedButton = nullptr;
            return true;
          }
          if (key == GLFW_KEY_ESCAPE) {
            hudSys.setView(&pauseView);
            return true;
          }
          return false;
        });

    std::unique_ptr<Network> network;
    auto addr = pauseView.container.addChild<UI::TextInput>(
        glm::vec2{0.f, 0.5f}, "server address");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{0.f, 0.6f}, glm::vec2{.2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
            try {
              network = std::make_unique<Network>(addr->content.c_str());
            } catch (const std::exception &e) {
              std::println("{}", e.what());
            }
        },
        "Connect");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{.2f, 0.6f}, glm::vec2{.2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS)
            network = nullptr;
        },
        "Disonnect");

    using namespace std::string_literals;

    glm::dvec2 worldCursorPos{};
    glm::vec2 worldYawAndPitch{};
    pauseView.container.addEventHandler<vkh::input::EventType::Key>(
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

    bool updateParticleSysAttractor = false;
    std::vector<glm::mat4> newTransform = genTransform();
    std::vector<glm::mat4> prevTransform = newTransform;
    float timeOfNewTransform = 0.f;

    worldView.container.addEventHandler<vkh::input::EventType::Key>(
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
          if (key == GLFW_KEY_R &&
              (action == GLFW_PRESS || action == GLFW_REPEAT)) {
            updateParticleSysAttractor = true;
            timeOfNewTransform = context.time;
            return true;
          }
          if (key == GLFW_KEY_G && action == GLFW_PRESS) {
            for (auto &mat : newTransform) {
              std::println("{}", glm::to_string(mat));
            }
          }
          return false;
        });
    worldView.container.addEventHandler<vkh::input::EventType::WindowFocus>(
        [&](int focused) {
          audioFadeBegin = std::chrono::high_resolution_clock::now();
          return false;
        });

    canvasView.container.addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(key == GLFW_KEY_ESCAPE && action == GLFW_PRESS))
            return false;
          hudSys.setView(&pauseView);
          return true;
        });
    hudSys.setView(&worldView);

    {
      size_t id =
          hudSys.solidColorSys.addTextureFromFile("textures/crosshair.png");
      const float sizeOver2 = 0.02f;
      auto crosshair = worldView.container.addChild<UI::RectImg>(
          glm::vec2{.5f - sizeOver2}, glm::vec2{sizeOver2 * 2}, id);
    }

    auto fpsRect = worldView.container.addChild<UI::RectImg>(
        glm::vec2{}, glm::vec2{.3f, .3f}, 0);
    auto fpsText = fpsRect->addChild<UI::Text>(glm::vec2{});

    auto orientationTxt =
        worldView.container.addChild<UI::Text>(glm::vec2{1.f, -1.f});

    vkh::EntitySys::Entity *lastPicked = nullptr;

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

      UI::animateBubbly(context, hudSys.getView()->container);

      // featherDuckGuard.update();

      float deltaTime =
          std::chrono::duration<float>(newTime - audioFadeBegin).count();
      float volume;
      if (context.window.isFocused()) {
        volume = glm::clamp(deltaTime * audioFadeSpeed, 0.f, 1.f);
      } else {
        volume = glm::clamp(1.f - deltaTime * audioFadeSpeed, 0.f, 1.f);
      }
      vkh::audio::setVolume(volume);

      // if (network) {
      //   bool needUpdate = false;
      //
      //   std::vector<uint8_t> pktData;
      //   while (network->receive(pktData)) {
      //     if (pktData.size() >= sizeof(Packet)) {
      //       needUpdate = true;
      //       Packet *p = reinterpret_cast<Packet *>(pktData.data());
      //
      //       if (p->type == PacketType::Join) {
      //         playersIndices[p->id] = entities.size();
      //         for (size_t i = 0; i < playerModel->meshes.size(); i++)
      //           entities.emplace_back(vkh::EntitySys::Transform{.position{5.f}},
      //                                 vkh::EntitySys::RigidBody{},
      //                                 playerModel, i, p->id);
      //       } else if (p->type == PacketType::Leave) {
      //         for (int i = 0; i < entities.size();) {
      //           if (entities[i].id == p->id) {
      //             entities[i] = entities.back();
      //             entities.pop_back();
      //           } else {
      //             ++i;
      //           }
      //         }
      //       } else if (p->type == PacketType::Update &&
      //                  pktData.size() >= sizeof(UpdatePacket)) {
      //         UpdatePacket *up =
      //             reinterpret_cast<UpdatePacket *>(pktData.data());
      //         if (playersIndices.contains(up->id)) {
      //           auto begin = entities.begin() + playersIndices[up->id];
      //           for (size_t i = 0; i < playerModel->meshes.size(); i++) {
      //             (begin + i)->transform.position = up->position;
      //             (begin + i)->transform.orientation = up->orientation;
      //           }
      //         }
      //       }
      //     }
      //   }
      //
      // Send local player position to the server
      // UpdatePacket myUpdate;
      // myUpdate.type = PacketType::Update;
      // myUpdate.id = 0; // Overwritten by server
      // myUpdate.position = context.camera.position;
      // myUpdate.orientation = context.camera.orientation;
      //
      // network->send(&myUpdate, sizeof(myUpdate));
      // if (needUpdate)
      //   entitySys.updateBuffers();
      // }

      static bool dontDoOnce = true;
      if (!dontDoOnce) {
        if (!context.window.isFocused())
          continue;
      }
      dontDoOnce = false;

      fpsText->content =
          std::format("FPS: {}", static_cast<int>(1.f / frameTime));

      fpsRect->setAbsoluteSize(fpsText->getAbsoluteSize());
      orientationTxt->setPosition(1.f -
                                  glm::vec2{orientationTxt->getSize().x, 0.f});
      orientationTxt->content = std::format(
          "Yaw: {}\nPitch:{}", context.camera.yaw, context.camera.pitch);

      currentTime = newTime;

      // Disable collisions with entities by providing empty array
      std::vector<vkh::EntitySys::Entity> empty;
      vkh::input::update(context, empty);

      // Entity picking visualization
      {
        auto pointed = entitySys.getPointingAt(1.0f);
        if (pointed != lastPicked) {
          if (lastPicked) {
            lastPicked->color = glm::vec4(1.0f); // Reset
          }

          lastPicked = pointed;

          if (lastPicked) {
            lastPicked->color =
                glm::vec4(2.0f, 0.5f, 0.5f, 1.0f); // Highlight Red-ish
          }
        }
      }

      entitySys.updateBuffers();

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

          if (updateParticleSysAttractor) {
            prevTransform = newTransform;
            newTransform = genTransform();
            updateParticleSysAttractor = false;
          }
          std::vector<glm::mat4> blendedTransform(newTransform.size());
          for (size_t i = 0; i < newTransform.size(); i++) {
            if (i > prevTransform.size() - 1) {
              blendedTransform[i] = glm::mat4{0.f};
              continue;
            }
            float p = (context.time - timeOfNewTransform) * .5f;
            blendedTransform[i] =
                glm::mix(prevTransform[i], newTransform[i], p);
          }
          particleSys.setAttractor(blendedTransform);

          particleSys.update();

          entitySys.updateJoints();
          entitySys.cull(commandBuffer);
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
        hudSys.render();

        vkCmdNextSubpass(commandBuffer, VK_SUBPASS_CONTENTS_INLINE);

        // 1x subpass
        if (hudSys.getView() == &worldView) {
          particleSys.render();
        }
        if (hudSys.getView() == &smokeView) {
          smokeSys.render();
        }

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
