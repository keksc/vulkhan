#pragma once

#include "rect.hpp"
#include "eventListener.hpp"

namespace vkh {
namespace hud {
class Button
    : public Rect,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton> {
public:
  Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec3 color, std::function<void(int, int, int)> onClick)
      : Rect(view, parent, position, size, color), onClick{onClick},
        EventListener(view, [this](int button, int action, int mods) {
          mouseButtonCallback(button, action, mods);
        }) {}

private:
  std::function<void(int, int, int)> onClick;
  void mouseButtonCallback(int button, int action, int mods) {
    const auto &cursorPos = view.context.input.cursorPos;

    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(cursorPos, min)) &&
        glm::all(glm::lessThanEqual(cursorPos, max)))
      onClick(button, action, mods);
  }
};
} // namespace hud
} // namespace vkh
