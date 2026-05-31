#include "element.hpp"

#include "view.hpp"

namespace vkh {
namespace hud {
Element::Element(View &view, Element *parent, glm::vec2 position,
                 glm::vec2 size)
    : view{view}, parent{parent}, relPosition{position}, relSize{size} {
  updateAbsolute();
  view.elementCount++;
}
Element::~Element() {
  children.clear();
  view.elementCount--;
}

void Element::updateAbsolute() {
  if (parent) {
    absPos = parent->absPos + relPosition * parent->absSize;
    absSize = parent->absSize * relSize;
  } else {
    absPos = relPosition * 2.f - 1.f;
    absSize = relSize * 2.f;
  }
  for (auto &child : children) {
    child->updateAbsolute();
  }
}

bool Element::isPositionInside(const glm::vec2 &pos) {
  const glm::vec2 min = glm::min(absPos, absPos + absSize);
  const glm::vec2 max = glm::max(absPos, absPos + absSize);
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

void Element::setPosition(glm::vec2 newPosition) {
  relPosition = newPosition;
  updateAbsolute();
}
void Element::setAbsolutePosition(glm::vec2 newPosition) {
  absPos = newPosition;
  if (parent) {
    relPosition = (absPos - parent->absPos) / parent->absSize;
  } else {
    relPosition = (absPos + 1.f) / 2.f;
  }
  for (auto &child : children) {
    child->updateAbsolute();
  }
}
glm::vec2 Element::getPosition() const { return relPosition; }
glm::vec2 Element::getAbsolutePosition() const { return absPos; }

void Element::setSize(glm::vec2 newSize) {
  relSize = newSize;
  updateAbsolute();
}
void Element::setAbsoluteSize(glm::vec2 newSize) {
  absSize = newSize;
  if (parent) {
    relSize = absSize / parent->absSize;
  } else {
    relSize = absSize / 2.f;
  }
  for (auto &child : children) {
    child->updateAbsolute();
  }
}
glm::vec2 Element::getSize() const { return relSize; }
glm::vec2 Element::getAbsoluteSize() const { return absSize; }
} // namespace hud
} // namespace vkh
