#include "clickable.hpp"

namespace UI {
Clickable::Clickable(vkh::hud::View &view, Element *parent, glm::vec2 position,
                     glm::vec2 size, std::function<void(int, int, int)> onClick)
    : Element(view, parent, position, size), onClick{onClick} {}
void Clickable::setCallback(std::function<void(int, int, int)> newCallback) {
  onClick = std::move(newCallback);
}
bool Clickable::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT)
    return false;
  if (action == GLFW_PRESS && isCursorInside()) {
    pressed = true;
    return true;
  }
  if (action == GLFW_RELEASE && pressed) {
    pressed = false;
    // Existing button callbacks consume GLFW_PRESS; activate only after a
    // completed click so dragging off a button safely cancels the action.
    if (isCursorInside() && onClick)
      onClick(button, GLFW_PRESS, mods);
    return true;
  }
  return false;
}
} // namespace UI
