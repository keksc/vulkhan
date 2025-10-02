#include "button.hpp"

namespace vkh::hud {

Button::Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
               decltype(Rect::imageIndex) imageIndex,
               std::function<void(int, int, int)> onClick,
               const std::string &label)
    : Rect(view, parent, position, size, imageIndex), onClick{onClick},
      label{addChild<Text>(glm::vec2{}, label)} {}
void Button::setCallback(std::function<void(int, int, int)> newCallback) {
  onClick = std::move(newCallback);
}
bool Button::handleMouseButton(int button, int action, int mods) {
  if (isCursorInside()) {
    onClick(button, action, mods);
    return true;
  }
  return false;
}
} // namespace vkh::hud
