#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Line : public Element {
public:
  Line(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       glm::vec3 color)
      : Element(view, parent, position, size), color{color} {};
  glm::vec3 color{};

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    drawInfo.solidColorLineVertices.emplace_back(
        glm::vec2{position.x, position.y}, color);
    drawInfo.solidColorLineVertices.emplace_back(
        glm::vec2{position.x + size.x, position.y + size.y}, color);
  };
};
} // namespace hud
} // namespace vkh
