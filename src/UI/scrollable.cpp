#include "scrollable.hpp"

namespace UI {
Scrollable::Scrollable(vkh::hud::View &view, Element *parent,
                       glm::vec2 position, glm::vec2 size, glm::vec2 scale,
                       glm::vec2 min, glm::vec2 max)
    : Element(view, parent, position, size), scale{scale}, min{min}, max{max} {}
bool Scrollable::handleScroll(double xoffset, double yoffset) {
  glm::vec2 targetScroll = scroll + (glm::vec2{xoffset, yoffset} * scale);

  targetScroll.x = glm::clamp(targetScroll.x, -max.x, -min.x);
  targetScroll.y = glm::clamp(targetScroll.y, -max.y, -min.y);

  glm::vec2 allowedScrollDelta = targetScroll - scroll;

  if (glm::length2(allowedScrollDelta) < 0.00001f) {
    return false;
  }

  scroll = targetScroll;

  for (auto child : children) {
    child->setAbsolutePosition(child->getAbsolutePosition() +
                               allowedScrollDelta);
  }

  return true;
}
} // namespace UI
