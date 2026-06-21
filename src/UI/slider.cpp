#include "slider.hpp"

#include "../vkh/systems/hud/view.hpp"
#include "rectImg.hpp"

namespace UI {
Slider::Slider(vkh::hud::View &view, Element *parent, glm::vec2 position,
               glm::vec2 size, glm::vec2 bounds, float value)
    : Element(view, parent, position, size), bounds{bounds}, value{value} {

  orientation =
      (size.y > size.x) ? Orientation::Vertical : Orientation::Horizontal;

  float p = (value - bounds.x) / (bounds.y - bounds.x);
  if (orientation == Orientation::Vertical)
    p = 1.f - p;
  box = addChild<RectImg>(glm::vec2{.4f}, glm::vec2{.2f}, 0);
  addChild<RectImg>(glm::vec2{}, glm::vec2{1.f}, 0);

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
    p = (view.context.input.cursorPos.x - absPos.x) / absSize.x;
  } else {
    p = (view.context.input.cursorPos.y - absPos.y) / absSize.y;
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
    float x = glm::mix(absPos.x, absPos.x + absSize.x, p) -
              box->getAbsoluteSize().x / 2.f;
    float y = box->getAbsolutePosition().y;
    box->setAbsolutePosition(glm::vec2{x, y});
  } else {
    float x = box->getAbsolutePosition().x;
    float y = glm::mix(absPos.y, absPos.y + absSize.y, p) -
              box->getAbsoluteSize().y / 2.f;
    box->setAbsolutePosition(glm::vec2{x, y});
  }
}
} // namespace UI
