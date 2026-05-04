#include "view.hpp"

#include "element.hpp"

namespace vkh {
namespace hud {
View::View(EngineContext &context, HudSys &hudSys)
    : context{context}, hudSys{hudSys},
      // The conversion from [0, 1] range to Vulkan NDC implicitely happens here
      // {0, 0} is top left, {1, 1} is bottom right
      container{*this, nullptr, glm::vec2{-1.f}, glm::vec2{2.f}} {
  context.inputCallbackSystems[this] = {
      [this](int button, int action, int mods) {
        container.dispatchEvent<input::EventType::MouseButton>(button, action,
                                                               mods);
      },
      [this](int key, int scancode, int action, int mods) {
        container.dispatchEvent<input::EventType::Key>(key, scancode, action,
                                                       mods);
      },
      [this](double xpos, double ypos) {
        container.dispatchEvent<input::EventType::CursorPosition>(xpos, ypos);
      },
      [this](unsigned int codepoint) {
        container.dispatchEvent<input::EventType::Character>(codepoint);
      },
      [this](int count, const char **paths) {
        container.dispatchEvent<input::EventType::Drop>(count, paths);
      },
      [this](int focused) {
        container.dispatchEvent<input::EventType::WindowFocus>(focused);
      },
  };
}
View::~View() { context.inputCallbackSystems.erase(this); }
void View::setCurrent() { context.currentInputCallbackSystemKey = this; }
} // namespace hud
} // namespace vkh
