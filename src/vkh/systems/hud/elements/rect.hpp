#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Rect : public Element {
public:
  Rect(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       glm::vec3 color)
      : Element(view, parent, position, size), color{color} {};
  glm::vec3 color{};

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    float x0 = position.x;
    float x1 = x0 + size.x;
    float y0 = position.y;
    float y1 = y0 + size.y;

    uint32_t baseIndex =
        static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x0, y0}, color);
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x1, y0}, color);
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x1, y1}, color);
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x0, y1}, color);

    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 1);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 3);
  };
};
} // namespace hud
} // namespace vkh
