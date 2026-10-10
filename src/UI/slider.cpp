#include "slider.hpp"
#include "rectImg.hpp"
#include "sliderMath.hpp"
#include "theme.hpp"
namespace UI {
Slider::Slider(vkh::hud::View &view, Element *parent, glm::vec2 position,
               glm::vec2 size, glm::vec2 bounds, float value)
    : Element(view, parent, position, size), bounds(bounds), value(value) {
  orientation =
      size.y > size.x ? Orientation::Vertical : Orientation::Horizontal;
  theme::initialize(view);
  const bool vertical = orientation == Orientation::Vertical;
  addChild<RectImg>(vertical ? glm::vec2{.4f, 0} : glm::vec2{0, .4f},
                    vertical ? glm::vec2{.2f, 1} : glm::vec2{1, .2f},
                    theme::color(view, theme::border));
  fill = addChild<RectImg>(glm::vec2{}, glm::vec2{},
                           theme::color(view, theme::accent));
  box =
      addChild<RectImg>(glm::vec2{}, glm::vec2{}, theme::color(view, 0xffffff));
  this->value = sliderValue(
      bounds.y > bounds.x ? (value - bounds.x) / (bounds.y - bounds.x) : 0,
      bounds.x, bounds.y, step);
}
void Slider::updateFromCursor() {
  const bool vertical = orientation == Orientation::Vertical;
  const float extent = vertical ? absSize.y : absSize.x;
  if (extent <= 0)
    return;
  float p = vertical ? (view.context.input.cursorPos.y - absPos.y) / extent
                     : (view.context.input.cursorPos.x - absPos.x) / extent;
  if (vertical)
    p = 1 - p;
  const float next = sliderValue(p, bounds.x, bounds.y, step);
  if (next != value) {
    value = next;
    if (onChange)
      onChange(value);
  }
}
bool Slider::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT)
    return false;
  if (action == GLFW_RELEASE && selected) {
    updateFromCursor();
    selected = false;
    if (onCommit)
      onCommit(value);
    return true;
  }
  if (action != GLFW_PRESS)
    return false;
  selected = isCursorInside();
  if (selected)
    updateFromCursor();
  return selected;
}
bool Slider::handleCursorPosition(double, double) {
  if (!selected)
    return false;
  updateFromCursor();
  return true;
}
void Slider::updateBoxPosition(float p) {
  const bool vertical = orientation == Orientation::Vertical;
  if (vertical)
    p = 1 - p;
  const glm::vec2 pixels = glm::vec2(view.context.window.size);
  if (pixels.x <= 0 || pixels.y <= 0)
    return;
  const glm::vec2 thumb = (vertical ? glm::vec2{18, 8} : glm::vec2{8, 18}) *
                          view.layoutScale * 2.f / pixels;
  box->setAbsoluteSize(thumb);
  box->setAbsolutePosition(absPos +
                           (vertical
                                ? glm::vec2{absSize.x * .5f, p * absSize.y}
                                : glm::vec2{p * absSize.x, absSize.y * .5f}) -
                           thumb / 2.f);
  fill->setPosition(vertical ? glm::vec2{.4f, p} : glm::vec2{0, .4f});
  fill->setSize(vertical ? glm::vec2{.2f, 1 - p} : glm::vec2{p, .2f});
  box->imageIndex = theme::color(
      view, selected || isCursorInside() ? theme::accent : 0xffffff);
}
void Slider::addToDrawInfo(vkh::hud::DrawInfo &, float) {
  if (selected && glfwGetMouseButton(view.context.window,
                                     GLFW_MOUSE_BUTTON_LEFT) != GLFW_PRESS) {
    selected = false;
    if (onCommit)
      onCommit(value);
  }
  updateBoxPosition(
      bounds.y > bounds.x
          ? glm::clamp((value - bounds.x) / (bounds.y - bounds.x), 0.f, 1.f)
          : 0.f);
}
} // namespace UI
