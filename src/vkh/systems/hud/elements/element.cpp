#include "element.hpp"

#include "view.hpp"

namespace vkh {
namespace hud {
Element::Element(View &view, Element *parent, glm::vec2 position,
                 glm::vec2 size)
    : position{position}, size{size}, view{view}, parent{parent} {
  if (parent) {
    this->position = parent->position + position * parent->size;
    this->size = parent->size * size;
  }
  view.elementCount++;
}
Element::~Element() {
  children.clear();
  view.elementCount--;
}
bool Element::isPositionInside(const glm::vec2 &pos) {
  const glm::vec2 min = glm::min(position, position + size);
  const glm::vec2 max = glm::max(position, position + size);
  return glm::all(glm::greaterThanEqual(pos, min)) &&
         glm::all(glm::lessThanEqual(pos, max));
}
bool Element::isCursorInside() {
  return isPositionInside(view.context.input.cursorPos);
}
bool Element::handleMouseButton(int button, int action, int mods) {
  return false;
}
bool Element::handleKey(int key, int scancode, int action, int mods) {
  return false;
}
bool Element::handleCursorPosition(double xpos, double ypos) { return false; }
bool Element::handleCharacter(unsigned int codepoint) { return false; }
bool Element::handleDrop(int count, const char **paths) { return false; }
} // namespace hud
} // namespace vkh
