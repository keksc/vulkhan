#include "UI.hpp"
#include "UI/layout.hpp"

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
#include "settings.hpp"
#include "skyResolutionPresets.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/input.hpp"
#include "vkh/systems/hud/hud.hpp"
#include "vkh/systems/hud/view.hpp"

#include "UI/bindEdit.hpp"
#include "UI/button.hpp"
#include "UI/canvas.hpp"
#include "UI/rectImg.hpp"
#include "UI/scrollable.hpp"
#include "UI/slider.hpp"
#include "UI/stylizedBtn.hpp"
#include "UI/text.hpp"
#include "UI/textInput.hpp"
#include "UI/theme.hpp"
#include "graphicsPresets.hpp"
#include <cctype>

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
    glfwSetWindowMonitor(context.window, nullptr, windowedPos.x, windowedPos.y,
                         windowedSize.x, windowedSize.y, 0);
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
  vkh::hud::View controlsView;
  vkh::hud::View canvasView;

  std::unique_ptr<Network> network;

  glm::ivec2 layoutViewport{};
  std::shared_ptr<UI::RectImg> crosshair;
  std::vector<std::pair<vkh::hud::View *, std::shared_ptr<UI::RectImg>>>
      backdrops;
  std::shared_ptr<UI::RectImg> fpsRect;
  std::shared_ptr<UI::Text> fpsText;
  std::shared_ptr<UI::Text> orientationTxt;

  std::shared_ptr<UI::BindEdit> selectedButton;

  GameUI::SkyResolutionHandler skyResolutionHandler;
  GameUI::GraphicsSettingsHandler graphicsSettingsHandler;

  // Preserve the view direction across pause pages and cursor-mode changes.
  glm::vec2 worldYawAndPitch{};

  explicit Impl(vkh::EngineContext &context)
      : hudSys(context), worldView(context, hudSys), pauseView(context, hudSys),
        settingsView(context, hudSys), controlsView(context, hudSys),
        canvasView(context, hudSys) {
    UI::theme::initialize(pauseView);
    applyFullscreen(context, vkh::settings::current().fullscreen);
    auto page = [this](vkh::hud::View &view, const std::string &title) {
      auto background = view.container.addChild<UI::RectImg>(
          glm::vec2{}, glm::vec2{1},
          UI::theme::color(view, UI::theme::background));
      backdrops.emplace_back(&view, background);
      view.container.addChild<UI::RectImg>(
          glm::vec2{.08f, .10f}, glm::vec2{.84f, .80f},
          UI::theme::color(view, UI::theme::panel));
      view.container.addChild<UI::Text>(glm::vec2{.12f, .145f}, title,
                                        glm::vec3{1.f}, 1.15f);
    };
    page(pauseView, "VULKHAN");
    page(settingsView, "Graphics");
    page(controlsView, "Controls");
    pauseView.container.addChild<UI::Text>(glm::vec2{.13f, .215f}, "Paused",
                                           glm::vec3{.7f}, .75f);
    auto canvasBackdrop = canvasView.container.addChild<UI::RectImg>(
        glm::vec2{}, glm::vec2{1.f},
        UI::theme::color(canvasView, UI::theme::background));
    backdrops.emplace_back(&canvasView, canvasBackdrop);
    canvasView.container.addChild<UI::Canvas>(
        glm::vec2{}, glm::vec2{1},
        UI::theme::color(canvasView, UI::theme::panel));
    pauseView.container.addChild<UI::StylizedBtn>(
        glm::vec2{.10f, .29f}, glm::vec2{.44f, .11f}, 0,
        [this, &context](int, int, int) { resume(context); }, "Resume game");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{.13f, .43f}, glm::vec2{.38f, .075f}, 0,
        [this](int, int, int) { hudSys.setView(&settingsView); },
        "Graphics settings");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{.13f, .53f}, glm::vec2{.38f, .075f}, 0,
        [this](int, int, int) { hudSys.setView(&controlsView); }, "Controls");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{.13f, .63f}, glm::vec2{.38f, .075f}, 0,
        [this](int, int, int) { hudSys.setView(&canvasView); },
        "Drawing canvas");
    pauseView.container.addChild<UI::Text>(
        glm::vec2{.13f, .79f}, "ESC resumes play", glm::vec3{.7f}, .7f);

    auto addToggle = [this, &context](vkh::hud::View &view, glm::vec2 position,
                                      const char *name,
                                      bool vkh::Settings::*field) {
      auto label = [](const std::string &name, bool value) {
        return name + ": " + (value ? "On" : "Off");
      };
      auto btn = view.container.addChild<UI::Button>(
          position, glm::vec2{.35f, &view == &settingsView ? .055f : .075f}, 0,
          [](int, int, int) {}, label(name, vkh::settings::current().*field));
      btn->setCallback([this, &context, weak = std::weak_ptr(btn), field,
                        name = std::string(name), label](int, int, int) {
        auto &settings = vkh::settings::current();
        settings.*field = !(settings.*field);
        vkh::settings::save();
        if (auto button = weak.lock())
          button->label->content = label(name, settings.*field);
        if (graphicsSettingsHandler)
          graphicsSettingsHandler(settings);
        if (field == &vkh::Settings::fullscreen)
          applyFullscreen(context, settings.fullscreen);
        if (field == &vkh::Settings::vsync)
          context.window.framebufferResized = true;
      });
    };
    auto addCycle = [this](glm::vec2 position, const std::string &name,
                           uint32_t vkh::Settings::*field,
                           std::vector<uint32_t> values,
                           std::vector<std::string> labels, bool sky = false) {
      auto text = [name, values, labels](uint32_t value) {
        auto found = std::find(values.begin(), values.end(), value);
        size_t index = found == values.end() ? 0 : found - values.begin();
        return name + ": " + labels[index];
      };
      auto button = settingsView.container.addChild<UI::Button>(
          position, glm::vec2{.35f, .055f}, 0, [](int, int, int) {},
          text(vkh::settings::current().*field));
      button->setCallback([this, weak = std::weak_ptr(button), field, values,
                           text, sky](int, int, int) {
        auto &settings = vkh::settings::current();
        auto found = std::find(values.begin(), values.end(), settings.*field);
        size_t index = found == values.end()
                           ? 0
                           : (found - values.begin() + 1) % values.size();
        settings.*field = values[index];
        vkh::settings::save();
        if (auto button = weak.lock())
          button->label->content = text(settings.*field);
        if (graphicsSettingsHandler)
          graphicsSettingsHandler(settings);
        if (sky && skyResolutionHandler)
          skyResolutionHandler(settings.skyMilkyWayFaceSize,
                               settings.skyDiscTurbulenceSize);
      });
    };
    addToggle(settingsView, {.12f, .24f}, "V-Sync", &vkh::Settings::vsync);
    addToggle(settingsView, {.12f, .31f}, "Fullscreen",
              &vkh::Settings::fullscreen);
    addToggle(settingsView, {.12f, .38f}, "Show FPS", &vkh::Settings::showFps);
    addCycle({.12f, .52f}, "Grass distance", &vkh::Settings::grassDistance,
             {40, 80, 120}, {"Near", "Medium", "Far"});
    addCycle({.12f, .59f}, "Terrain shadows",
             &vkh::Settings::terrainShadowSteps, {0, 8, 16, 24},
             {"Off", "Low", "Medium", "High"});
    addToggle(settingsView, {.12f, .45f}, "Grass", &vkh::Settings::drawGrass);
    auto addSlider = [this](glm::vec2 position, const std::string &name,
                            uint32_t vkh::Settings::*field, glm::vec2 range,
                            const std::string &unit) {
      auto label = settingsView.container.addChild<UI::Text>(
          position,
          std::format("{}: {}{}", name, vkh::settings::current().*field, unit),
          glm::vec3{1.f}, .6f);
      auto slider = settingsView.container.addChild<UI::Slider>(
          position + glm::vec2{0, .027f}, glm::vec2{.35f, .028f}, range,
          float(vkh::settings::current().*field));
      slider->onChange = [this, field, weak = std::weak_ptr(label), name,
                          unit](float value) {
        auto &settings = vkh::settings::current();
        settings.*field = uint32_t(value);
        if (auto text = weak.lock())
          text->content = std::format("{}: {}{}", name, settings.*field, unit);
        if (graphicsSettingsHandler)
          graphicsSettingsHandler(settings);
      };
      slider->onCommit = [](float) { vkh::settings::save(); };
    };
    addSlider({.12f, .66f}, "Field of view", &vkh::Settings::fieldOfView,
              {70, 120}, " deg");
    addToggle(settingsView, {.53f, .52f}, "Post effects",
              &vkh::Settings::postEffects);
    addSlider({.53f, .59f}, "Light shafts", &vkh::Settings::lightShaftStrength,
              {0, 150}, "%");
    std::vector<uint32_t> samples;
    std::vector<std::string> sampleLabels;
    const auto &limits = context.vulkan.physicalDeviceProperties.limits;
    uint32_t supported =
        static_cast<uint32_t>(limits.framebufferColorSampleCounts &
                              limits.framebufferDepthSampleCounts);
    for (auto count : vkh::msaaPresets)
      if (supported & count) {
        samples.push_back(count);
        sampleLabels.push_back(count == 1 ? "Off"
                                          : std::format("{}x MSAA", count));
      }
    addCycle({.53f, .24f}, "Antialiasing (restart)",
             &vkh::Settings::msaaSamples, samples, sampleLabels);
    addToggle(settingsView, {.53f, .31f}, "Cache sky (restart)",
              &vkh::Settings::useCachedSkyBake);
    auto sizes = [](std::span<const uint32_t> presets) {
      std::vector<std::string> labels;
      for (auto value : presets)
        labels.push_back(std::to_string(value));
      return labels;
    };
    addCycle({.53f, .38f}, "Milky way resolution",
             &vkh::Settings::skyMilkyWayFaceSize,
             {std::begin(vkh::milkyWayFaceSizePresets),
              std::end(vkh::milkyWayFaceSizePresets)},
             sizes(vkh::milkyWayFaceSizePresets), true);
    addCycle({.53f, .45f}, "Disc resolution",
             &vkh::Settings::skyDiscTurbulenceSize,
             {std::begin(vkh::discTurbulenceSizePresets),
              std::end(vkh::discTurbulenceSizePresets)},
             sizes(vkh::discTurbulenceSizePresets), true);
    settingsView.container.addChild<UI::Text>(
        glm::vec2{.12f, .745f},
        "Saved automatically. AA/cache need restart; shafts need post effects.",
        glm::vec3{.7f}, .65f);
    settingsView.container.addChild<UI::Button>(
        glm::vec2{.12f, .82f}, glm::vec2{.20f, .055f}, 0,
        [this](int, int, int) { hudSys.setView(&pauseView); }, "Back");
    settingsView.container.addChild<UI::Button>(
        glm::vec2{.67f, .82f}, glm::vec2{.20f, .055f}, 0,
        [this](int, int, int) { hudSys.setView(&controlsView); }, "Controls");
    addToggle(controlsView, {.12f, .24f}, "Invert Y axis",
              &vkh::Settings::invertYAxis);
    size_t i = 0;
    for (auto &[action, bind] : vkh::input::keybinds) {
      const float x = i < 5 ? .12f : .53f, y = .35f + .075f * (i % 5);
      auto button = controlsView.container.addChild<UI::BindEdit>(
          glm::vec2{x, y}, glm::vec2{.35f, .06f}, 0, [](int, int, int) {},
          bindingLabel(action));
      button->action = action;
      button->setCallback([this, weak = std::weak_ptr(button)](int, int, int) {
        if (selectedButton)
          selectedButton->label->content = bindingLabel(selectedButton->action);
        selectedButton = weak.lock();
        if (selectedButton)
          selectedButton->label->content = "Press a key... (ESC cancels)";
      });
      ++i;
    }
    controlsView.container.addChild<UI::Text>(
        glm::vec2{.12f, .76f}, "Click a binding to change it. ESC cancels.",
        glm::vec3{.7f}, .65f);
    controlsView.container.addChild<UI::Button>(
        glm::vec2{.12f, .82f}, glm::vec2{.20f, .055f}, 0,
        [this](int, int, int) {
          cancelBinding();
          hudSys.setView(&pauseView);
        },
        "Back");
    controlsView.container.addEventHandler<vkh::input::EventType::Key>(
        [this](int key, int, int action, int) {
          if (action != GLFW_PRESS)
            return false;
          if (selectedButton) {
            if (key != GLFW_KEY_ESCAPE && key != GLFW_KEY_UNKNOWN)
              vkh::input::keybinds[selectedButton->action] = key;
            cancelBinding();
            return true;
          }
          if (key == GLFW_KEY_ESCAPE) {
            hudSys.setView(&pauseView);
            return true;
          }
          return false;
        });
    settingsView.container.addEventHandler<vkh::input::EventType::Key>(
        [this](int key, int, int action, int) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            hudSys.setView(&pauseView);
            return true;
          }
          return false;
        });
    pauseView.container.addChild<UI::Text>(glm::vec2{.56f, .31f}, "Multiplayer",
                                           glm::vec3{1.f}, .85f);
    pauseView.container.addChild<UI::RectImg>(
        glm::vec2{.56f, .38f}, glm::vec2{.30f, .065f},
        UI::theme::color(pauseView, UI::theme::button));
    auto address = pauseView.container.addChild<UI::TextInput>(
        glm::vec2{.575f, .397f}, "127.0.0.1");
    address->setSizeMultiplicator(.8f);
    auto status = pauseView.container.addChild<UI::Text>(
        glm::vec2{.56f, .58f}, "Not connected", glm::vec3{.7f}, .7f);
    pauseView.container.addChild<UI::Button>(
        glm::vec2{.56f, .47f}, glm::vec2{.14f, .075f}, 0,
        [this, address, status](int, int, int) {
          try {
            network = std::make_unique<Network>(address->content.c_str());
            status->content = "Connected";
          } catch (const std::exception &error) {
            std::println("{}", error.what());
            status->content = "Connection failed";
          }
        },
        "Connect");
    pauseView.container.addChild<UI::Button>(
        glm::vec2{.72f, .47f}, glm::vec2{.14f, .075f}, 0,
        [this, status](int, int, int) {
          network = nullptr;
          status->content = "Disconnected";
        },
        "Disconnect");

    pauseView.container.addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (!(action == GLFW_PRESS && key == GLFW_KEY_ESCAPE))
            return false;
          resume(context);
          return true;
        });

    worldView.container.addEventHandler<vkh::input::EventType::Key>(
        [&](int key, int scancode, int action, int mods) {
          if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            worldYawAndPitch = {context.camera.yaw, context.camera.pitch};
            glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
            vkh::input::resetMouseLook();
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
      crosshair = worldView.container.addChild<UI::RectImg>(
          glm::vec2{.5f - sizeOver2}, glm::vec2{sizeOver2 * 2}, id);
    }

    fpsRect = worldView.container.addChild<UI::RectImg>(
        glm::vec2{}, glm::vec2{.3f, .3f},
        UI::theme::color(worldView, UI::theme::panel));
    fpsText = fpsRect->addChild<UI::Text>(glm::vec2{});

    orientationTxt =
        worldView.container.addChild<UI::Text>(glm::vec2{1.f, -1.f});
    resize(context.window.size);
  }

  static std::string bindingLabel(vkh::input::Action action) {
    std::string name;
    for (char c : magic_enum::enum_name(action)) {
      if (!name.empty() && std::isupper(static_cast<unsigned char>(c)))
        name += ' ';
      name += c;
    }
    return name + ": " + vkh::input::getKeyName(vkh::input::keybinds[action]);
  }
  void cancelBinding() {
    if (selectedButton)
      selectedButton->label->content = bindingLabel(selectedButton->action);
    selectedButton = nullptr;
  }
  void resume(vkh::EngineContext &context) {
    cancelBinding();
    context.camera.yaw = worldYawAndPitch.x;
    context.camera.pitch = worldYawAndPitch.y;
    glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
    vkh::input::resetMouseLook();
    hudSys.setView(&worldView);
  }

  static void refreshText(vkh::hud::Element &element) {
    if (auto *text = dynamic_cast<UI::Text *>(&element))
      text->refreshLayout();
    for (auto &child : element.children)
      refreshText(*child);
  }
  void resize(glm::ivec2 viewport) {
    if (viewport.x <= 0 || viewport.y <= 0)
      return;
    layoutViewport = viewport;
    hudSys.textSys.updateGlyphMetrics(viewport);
    const auto layout = UI::fitCanvas(viewport);
    for (auto &[view, background] : backdrops) {
      view->layoutScale = layout.scale;
      view->container.setAbsoluteSize(layout.size);
      view->container.setAbsolutePosition(layout.position);
      background->setAbsolutePosition(glm::vec2{-1.f});
      background->setAbsoluteSize(glm::vec2{2.f});
    }
    for (auto *view :
         {&pauseView, &settingsView, &controlsView, &worldView, &canvasView})
      refreshText(view->container);
    const glm::vec2 size = glm::vec2{32.f} / glm::vec2(viewport) * 2.f;
    crosshair->setAbsoluteSize(size);
    crosshair->setAbsolutePosition(-size / 2.f);
  }

  void update(vkh::EngineContext &context, float frameTime) {
    if (layoutViewport != context.window.size)
      resize(context.window.size);
    UI::animateBubbly(context, hudSys.getView()->container);

    fpsText->content =
        vkh::settings::current().showFps
            ? std::format("FPS: {}", static_cast<int>(1.f / frameTime))
            : "";
    fpsRect->setAbsoluteSize(fpsText->getAbsoluteSize());

    orientationTxt->content = std::format(
        "Yaw: {}\nPitch:{}", context.camera.yaw, context.camera.pitch);
    orientationTxt->setAbsolutePosition(
        glm::vec2{1.f - orientationTxt->getAbsoluteSize().x, -1.f});
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

void GameUI::setGraphicsSettingsHandler(GraphicsSettingsHandler handler) {
  impl->graphicsSettingsHandler = std::move(handler);
  if (impl->graphicsSettingsHandler)
    impl->graphicsSettingsHandler(vkh::settings::current());
}
