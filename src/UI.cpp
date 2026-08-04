#include "UI.hpp"

#include <GLFW/glfw3.h>
#include <format>
#include <magic_enum/magic_enum.hpp>
#include <print>
#include <ranges>
#include <string>

#include "network.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/input.hpp"
#include "vkh/systems/hud/hud.hpp"
#include "vkh/systems/hud/view.hpp"

#include "UI/bindEdit.hpp"
#include "UI/button.hpp"
#include "UI/canvas.hpp"
#include "UI/rectImg.hpp"
#include "UI/scrollable.hpp"
#include "UI/stylizedBtn.hpp"
#include "UI/text.hpp"
#include "UI/textInput.hpp"

struct GameUI::Impl {
  vkh::HudSys hudSys;

  vkh::hud::View worldView;
  vkh::hud::View pauseView;
  vkh::hud::View settingsView;
  vkh::hud::View canvasView;
  vkh::hud::View smokeView;

  std::unique_ptr<Network> network;

  std::shared_ptr<UI::RectImg> fpsRect;
  std::shared_ptr<UI::Text> fpsText;
  std::shared_ptr<UI::Text> orientationTxt;

  std::shared_ptr<UI::BindEdit> selectedButton;

  // Cursor/camera state stashed when entering the pause menu so escaping
  // back out restores exactly where the player was looking/pointing.
  glm::dvec2 worldCursorPos{};
  glm::vec2 worldYawAndPitch{};

  explicit Impl(vkh::EngineContext &context)
      : hudSys(context), worldView(context, hudSys), pauseView(context, hudSys),
        settingsView(context, hudSys), canvasView(context, hudSys),
        smokeView(context, hudSys) {
    hudSys.solidColorSys.addTextureFromFile(
        "textures/hud.png"); // Will be default texture since at index 0

    auto canvas = canvasView.container.addChild<UI::Canvas>(glm::vec2{},
                                                            glm::vec2{1.f}, 0);

    auto canvasBtn = pauseView.container.addChild<UI::StylizedBtn>(
        glm::vec2{.8f, 0.f}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
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

    auto addr = pauseView.container.addChild<UI::TextInput>(
        glm::vec2{0.f, 0.5f}, "server address");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{0.f, 0.6f}, glm::vec2{.2f}, 0,
        [&, addr](int button, int action, int) {
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

    fpsRect = worldView.container.addChild<UI::RectImg>(glm::vec2{},
                                                        glm::vec2{.3f, .3f}, 0);
    fpsText = fpsRect->addChild<UI::Text>(glm::vec2{});

    orientationTxt =
        worldView.container.addChild<UI::Text>(glm::vec2{1.f, -1.f});
  }

  void update(vkh::EngineContext &context, float frameTime) {
    UI::animateBubbly(context, hudSys.getView()->container);

    fpsText->content =
        std::format("FPS: {}", static_cast<int>(1.f / frameTime));
    fpsRect->setAbsoluteSize(fpsText->getAbsoluteSize());

    orientationTxt->setPosition(1.f -
                                glm::vec2{orientationTxt->getSize().x, 0.f});
    orientationTxt->content = std::format(
        "Yaw: {}\nPitch:{}", context.camera.yaw, context.camera.pitch);
  }
};

GameUI::GameUI(vkh::EngineContext &context)
    : impl(std::make_unique<Impl>(context)) {}

GameUI::~GameUI() = default;

void GameUI::update(vkh::EngineContext &context, float frameTime) {
  impl->update(context, frameTime);
}

void GameUI::render() { impl->hudSys.render(); }

bool GameUI::isWorldViewActive() const {
  return impl->hudSys.getView() == &impl->worldView;
}

bool GameUI::isSmokeViewActive() const {
  return impl->hudSys.getView() == &impl->smokeView;
}

void GameUI::addWorldViewKeyHandler(KeyHandler handler) {
  impl->worldView.container.addEventHandler<vkh::input::EventType::Key>(
      std::move(handler));
}

void GameUI::addWorldViewFocusHandler(FocusHandler handler) {
  impl->worldView.container.addEventHandler<vkh::input::EventType::WindowFocus>(
      std::move(handler));
}
