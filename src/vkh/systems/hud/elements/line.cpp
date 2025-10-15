#include "line.hpp"
#include <glm/ext.hpp>

namespace vkh::hud {
Line::Line(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
           glm::vec3 color)
    : Element(view, parent, position, size), color{color} {};
void Line::addToDrawInfo(DrawInfo &drawInfo, float depth) {
  // TODO: this might be optimizable when nothing changes, maybe add a
  // "changed" flag
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{position.x, position.y, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{position.x + size.x, position.y + size.y, depth}, color);
};
bool Line::isPositionInside(const glm::vec2 &pos) {
  glm::vec2 position2 = position + size;
  // return glm::distance2(position, pos) + glm::distance2(position2, pos) ==
  //        glm::distance2(position, position2);
  return glm::abs(glm::distance2(position, pos) +
                  glm::distance2(position2, pos) -
                  glm::distance2(position, position2)) <= .1f;
  // return distance(A, C) + distance(B, C) == distance(A, B);
}
} // namespace vkh::hud
