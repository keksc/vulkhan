#pragma once

#include "drawInfo.hpp"
#include "view.hpp"

namespace vkh {
namespace hud {
class Element {
public:
  Element(View &view, Element *parent, glm::vec2 position, glm::vec2 offset)
      : position{position}, size{offset}, view{view} {
    if (parent) {
      this->position = parent->position + position * parent->size;
      this->size = parent->size * size;
    }
    view.elementCount++;
  }
  Element(const Element &) = delete;
  Element &operator=(const Element &) = delete;

  ~Element() {
    children.clear();
    view.elementCount--;
  }

  template <typename T, typename... Args>
  std::shared_ptr<T> addChild(Args &&...args) {
    auto element = std::make_shared<T>(view, this, std::forward<Args>(args)...);
    children.emplace_back(element);
    return element;
  }

  template <typename T, typename... Args>
  std::shared_ptr<T>
  insertChild(std::vector<std::shared_ptr<Element>>::iterator pos,
              Args &&...args) {
    auto element = std::make_shared<T>(view, this, std::forward<Args>(args)...);
    children.insert(pos, element);
    return element;
  }

  bool isPositionInside(const glm::vec2 &pos) {
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(pos, min)) &&
        glm::all(glm::lessThanEqual(pos, max)))
      return true;
    return false;
  }
  inline bool isCursorInside() {
    return isPositionInside(view.context.input.cursorPos);
  }

  virtual void addToDrawInfo(DrawInfo &drawInfo, float depth) {};
  std::vector<std::shared_ptr<Element>> children;

  glm::vec2 position{};
  glm::vec2 size{};

protected:
  View &view;
};
} // namespace hud
} // namespace vkh
