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
  }
  Element(const Element &) = delete;
  Element &operator=(const Element &) = delete;
  ~Element() { children.clear(); }

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

  virtual void addToDrawInfo(DrawInfo &drawInfo) {};
  std::vector<std::shared_ptr<Element>> children;

  glm::vec2 position{};
  glm::vec2 size{};

protected:
  View &view;
};
} // namespace hud
} // namespace vkh
