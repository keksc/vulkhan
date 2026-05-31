#include "clickable.hpp"

namespace vkh::hud {

Clickable::Clickable(View &view, Element *parent, glm::vec2 position,
                     glm::vec2 size, std::function<void(int, int, int)> onClick)
    : Element(view, parent, position, size), onClick{onClick} {}
void Clickable::setCallback(std::function<void(int, int, int)> newCallback) {
  onClick = std::move(newCallback);
}
bool Clickable::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
    return false;
  if (isCursorInside()) {
    onClick(button, action, mods);
    return true;
  }
  return false;
}
} // namespace vkh::hud
