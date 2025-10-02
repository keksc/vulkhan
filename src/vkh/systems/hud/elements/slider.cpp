#include "slider.hpp"

namespace vkh::hud {
Slider::Slider(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
               glm::vec2 bounds, float value)
    : Element(view, parent, position, size), bounds{bounds}, value{value} {

  orientation =
      (size.y > size.x) ? Orientation::Vertical : Orientation::Horizontal;

  float p = (value - bounds.x) / (bounds.y - bounds.x);
  if (orientation == Orientation::Vertical)
    p = 1.f - p;
  box = addChild<Rect>(glm::vec2{.4f}, glm::vec2{.2f}, 0);
  addChild<Rect>(glm::vec2{}, glm::vec2{1.f}, 0);

  updateBoxPosition(p);
}
bool Slider::handleMouseButton(int button, int action, int mods) {
  if (action != GLFW_PRESS || button != GLFW_MOUSE_BUTTON_LEFT)
    return false;
  selected = isCursorInside();
  return selected;
}
bool Slider::handleCursorPosition(double xpos, double ypos) {
  if (!selected)
    return false;

  const auto &winSize = view.context.window.size;
  float p;

  if (orientation == Orientation::Horizontal) {
    float normalizedX = static_cast<float>(xpos) / winSize.x * 2.f - 1.f;
    p = (normalizedX - position.x) / size.x;
  } else {
    float normalizedY = static_cast<float>(ypos) / winSize.y * 2.f - 1.f;
    p = (normalizedY - position.y) / size.y;
  }

  p = glm::clamp(p, 0.f, 1.f);
  updateBoxPosition(p);
  if (orientation == Orientation::Vertical)
    p = 1.f - p;
  value = glm::mix(bounds.x, bounds.y, p);
  return true;
}
void Slider::updateBoxPosition(float p) {
  if (orientation == Orientation::Horizontal) {
    box->position.x =
        glm::mix(position.x, position.x + size.x, p) - box->size.x / 2.f;
  } else {
    box->position.y =
        glm::mix(position.y, position.y + size.y, p) - box->size.y / 2.f;
  }
}
} // namespace vkh::hud
