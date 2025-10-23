#include "view.hpp"
#include "element.hpp"

namespace vkh {
namespace hud {
View::View(EngineContext &context, HudSys &hudSys)
    : context{context}, hudSys{hudSys} {
  context.inputCallbackSystems[this] = {
      [this](int button, int action, int mods) {
        for (auto &elem : elements) {
          if (elem->dispatchEvent<input::EventType::MouseButton>(button, action,
                                                                 mods)) {
            break;
          }
        }
      },
      [this](int key, int scancode, int action, int mods) {
        for (auto &elem : elements) {
          if (elem->dispatchEvent<input::EventType::Key>(key, scancode, action,
                                                         mods)) {
            break;
          }
        }
      },
      [this](double xpos, double ypos) {
        for (auto &elem : elements) {
          if (elem->dispatchEvent<input::EventType::CursorPosition>(xpos,
                                                                    ypos)) {
            break;
          }
        }
      },
      [this](unsigned int codepoint) {
        for (auto &elem : elements) {
          if (elem->dispatchEvent<input::EventType::Character>(codepoint)) {
            break;
          }
        }
      },
      [this](int count, const char **paths) {
        for (auto &elem : elements) {
          if (elem->dispatchEvent<input::EventType::Drop>(count, paths)) {
            break;
          }
        }
      }};
}
View::View(View &&other) noexcept
    : context(other.context), elements(std::move(other.elements)),
      elementCount(other.elementCount), hudSys(other.hudSys) {
  context.inputCallbackSystems[this] =
      std::move(context.inputCallbackSystems[&other]);
  context.inputCallbackSystems.erase(&other);
}
View::~View() {
  context.inputCallbackSystems.erase(this);
  elements.clear();
}
void View::setCurrent() { context.currentInputCallbackSystemKey = this; }
} // namespace hud
} // namespace vkh
