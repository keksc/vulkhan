#include "UI.hpp"

#include <GLFW/glfw3.h>
#include <cstddef>
#include <format>
#include <magic_enum/magic_enum.hpp>
#include <print>
#include <ranges>
#include <span>
#include <string>
#include <utility>

#include "network/network.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/input.hpp"
#include "settings.hpp"
#include "skyResolutionPresets.hpp"
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

namespace {

// Switches the window between fullscreen (on the primary monitor's current
// video mode) and windowed, restoring the last windowed position/size when
// coming back out. Only touches GLFW state -- no dependency on window.hpp
// internals beyond context.window already being a GLFWwindow*, which the
// rest of this file already assumes (glfwSetCursorPos(context.window, ...)
// etc).
void applyFullscreen(vkh::EngineContext &context, bool enabled) {
  static glm::ivec2 windowedPos{100, 100};
  static glm::ivec2 windowedSize{1280, 720};

  bool alreadyFullscreen = glfwGetWindowMonitor(context.window) != nullptr;
  if (enabled == alreadyFullscreen)
    return;

  if (enabled) {
    glfwGetWindowPos(context.window, &windowedPos.x, &windowedPos.y);
    glfwGetWindowSize(context.window, &windowedSize.x, &windowedSize.y);
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = glfwGetVideoMode(monitor);
    glfwSetWindowMonitor(context.window, monitor, 0, 0, mode->width,
                         mode->height, mode->refreshRate);
  } else {
    glfwSetWindowMonitor(context.window, nullptr, windowedPos.x,
                         windowedPos.y, windowedSize.x, windowedSize.y, 0);
  }
}

size_t indexOfPreset(std::span<const uint32_t> presets, uint32_t value) {
  for (size_t i = 0; i < presets.size(); ++i)
    if (presets[i] == value)
      return i;
  return presets.size() / 2; // fall back to the middle preset
}

} // namespace

struct GameUI::Impl {
  vkh::HudSys hudSys;

  vkh::hud::View worldView;
  vkh::hud::View pauseView;
  vkh::hud::View settingsView;
  vkh::hud::View canvasView;

  std::unique_ptr<Network> network;

  std::shared_ptr<UI::RectImg> fpsRect;
  std::shared_ptr<UI::Text> fpsText;
  std::shared_ptr<UI::Text> orientationTxt;

  std::shared_ptr<UI::BindEdit> selectedButton;

  GameUI::SkyResolutionHandler skyResolutionHandler;

  // Cursor/camera state stashed when entering the pause menu so escaping
  // back out restores exactly where the player was looking/pointing.
  glm::dvec2 worldCursorPos{};
  glm::vec2 worldYawAndPitch{};

  explicit Impl(vkh::EngineContext &context)
      : hudSys(context), worldView(context, hudSys), pauseView(context, hudSys),
        settingsView(context, hudSys), canvasView(context, hudSys)
         {
    hudSys.solidColorSys.addTextureFromFile(
        "textures/hud.png"); // Will be default texture since at index 0

    // Apply settings that need to take effect immediately at launch (the
    // ones only ever read on a toggle, like useCachedSkyBake, don't need
    // this -- they're picked up next time whoever reads them runs).
    applyFullscreen(context, vkh::settings::current().fullscreen);

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
    auto settingsBtn = pauseView.container.addChild<UI::Button>(
        glm::vec2{}, glm::vec2{.2f, .2f}, 0,
        [&](int button, int action, int) {
          if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
            hudSys.setView(&settingsView);
          }
        },
        "Edit settings");

    float y = 0.f;

    // Persistent settings toggles. Each one flips a bool in
    // vkh::settings::current() and immediately persists the whole struct
    // to settings.bin via vkh::settings::save(), so changes survive a
    // crash rather than only a clean exit.
    //
    // These reuse UI::BindEdit purely as "a button with a label whose
    // text/callback can be changed after construction" (that's the only
    // widget in this file exposing setCallback()/label publicly). Their
    // `action` field is never touched, and they're never assigned to
    // `selectedButton`, so they never enter the keybind-rebinding flow
    // that field is normally used for.
    {
      struct ToggleDef {
        const char *label;
        bool vkh::Settings::*field;
      };
      static const ToggleDef toggles[] = {
          {"V-Sync", &vkh::Settings::vsync},
          {"Show FPS", &vkh::Settings::showFps},
          {"Invert Y axis", &vkh::Settings::invertYAxis},
          {"Fullscreen", &vkh::Settings::fullscreen},
          {"Cache sky bake", &vkh::Settings::useCachedSkyBake},
      };

      auto labelFor = [](const char *name, bool value) {
        return std::string(name) + ": " + (value ? "On" : "Off");
      };

      for (const auto &toggle : toggles) {
        auto toggleBtn = settingsView.container.addChild<UI::BindEdit>(
            glm::vec2{0.6f, y}, glm::vec2{.1f}, 0, [](int, int, int) {},
            labelFor(toggle.label, vkh::settings::current().*toggle.field));
        // Size the button to fit its label instead of a guessed fixed
        // size -- getAbsoluteSize()/setAbsoluteSize() work in pixel space
        // (same pattern as fpsRect below), so this holds regardless of
        // this container's relative scale, unlike getSize()/setSize().
        toggleBtn->setAbsoluteSize(toggleBtn->label->getAbsoluteSize());

        toggleBtn->setCallback([&, toggleBtn, field = toggle.field,
                                name = toggle.label](int button, int action,
                                                     int) {
          if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
            return;
          auto &s = vkh::settings::current();
          s.*field = !(s.*field);
          vkh::settings::save();
          toggleBtn->label->content = labelFor(name, s.*field);
          toggleBtn->setAbsoluteSize(toggleBtn->label->getAbsoluteSize());

          // Most settings are just read wherever they're needed (e.g.
          // useCachedSkyBake, read once in main.cpp at SkySys
          // construction) and don't need anything here. Fullscreen is the
          // exception: it has to take effect on the already-running
          // window right away.
          if (field == &vkh::Settings::fullscreen)
            applyFullscreen(context, s.fullscreen);
        });
        y += 0.1f;
      }
    }

    // Sky bake resolution: two independent cycling buttons (milky way
    // cubemap face size, disc turbulence texture size -- see
    // skyResolutionPresets.hpp), each persisting its own setting.
    // skyResolutionHandler always gets called with *both* current values
    // together, since SkySys::rebake() takes both at once -- so changing
    // one button re-sends the other setting's already-current value
    // unchanged.
    {
      auto fireRebake = [&] {
        auto &s = vkh::settings::current();
        if (skyResolutionHandler)
          skyResolutionHandler(s.skyMilkyWayFaceSize, s.skyDiscTurbulenceSize);
      };

      auto &s0 = vkh::settings::current();
      auto milkyWayBtn = settingsView.container.addChild<UI::BindEdit>(
          glm::vec2{0.6f, y}, glm::vec2{.1f}, 0, [](int, int, int) {},
          std::format("Milky way res: {}", s0.skyMilkyWayFaceSize));
      milkyWayBtn->setAbsoluteSize(milkyWayBtn->label->getAbsoluteSize());
      milkyWayBtn->setCallback([&, milkyWayBtn, fireRebake](int button,
                                                            int action, int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
          return;
        auto &s = vkh::settings::current();
        size_t current =
            indexOfPreset(vkh::milkyWayFaceSizePresets, s.skyMilkyWayFaceSize);
        size_t next = (current + 1) % std::size(vkh::milkyWayFaceSizePresets);
        s.skyMilkyWayFaceSize = vkh::milkyWayFaceSizePresets[next];
        vkh::settings::save();

        milkyWayBtn->label->content =
            std::format("Milky way res: {}", s.skyMilkyWayFaceSize);
        milkyWayBtn->setAbsoluteSize(milkyWayBtn->label->getAbsoluteSize());

        fireRebake();
      });
      y += 0.1f;

      auto discBtn = settingsView.container.addChild<UI::BindEdit>(
          glm::vec2{0.6f, y}, glm::vec2{.1f}, 0, [](int, int, int) {},
          std::format("Disc turbulence res: {}", s0.skyDiscTurbulenceSize));
      discBtn->setAbsoluteSize(discBtn->label->getAbsoluteSize());
      discBtn->setCallback([&, discBtn, fireRebake](int button, int action,
                                                     int) {
        if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
          return;
        auto &s = vkh::settings::current();
        size_t current = indexOfPreset(vkh::discTurbulenceSizePresets,
                                       s.skyDiscTurbulenceSize);
        size_t next =
            (current + 1) % std::size(vkh::discTurbulenceSizePresets);
        s.skyDiscTurbulenceSize = vkh::discTurbulenceSizePresets[next];
        vkh::settings::save();

        discBtn->label->content =
            std::format("Disc turbulence res: {}", s.skyDiscTurbulenceSize);
        discBtn->setAbsoluteSize(discBtn->label->getAbsoluteSize());

        fireRebake();
      });
      y += 0.1f;
    }

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
      kbEdit->setAbsoluteSize(kbEdit->label->getAbsoluteSize());
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
              selectedButton->setAbsoluteSize(selectedButton->label->getAbsoluteSize());
              vkh::input::keybinds[selectedButton->action] = GLFW_KEY_UNKNOWN;
              selectedButton = nullptr;
              return true;
            }
            selectedButton->label->content =
                static_cast<std::string>(
                    magic_enum::enum_name(selectedButton->action)) +
                ":" + vkh::input::getKeyName(key);
            selectedButton->setAbsoluteSize(selectedButton->label->getAbsoluteSize());
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

    fpsText->content = vkh::settings::current().showFps
                           ? std::format("FPS: {}",
                                        static_cast<int>(1.f / frameTime))
                           : "";
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

void GameUI::addWorldViewKeyHandler(KeyHandler handler) {
  impl->worldView.container.addEventHandler<vkh::input::EventType::Key>(
      std::move(handler));
}

void GameUI::addWorldViewFocusHandler(FocusHandler handler) {
  impl->worldView.container.addEventHandler<vkh::input::EventType::WindowFocus>(
      std::move(handler));
}

void GameUI::setSkyResolutionHandler(SkyResolutionHandler handler) {
  impl->skyResolutionHandler = std::move(handler);
}
