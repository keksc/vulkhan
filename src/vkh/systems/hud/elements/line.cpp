#include "line.hpp"

#include <glm/ext.hpp>
#include <glm/gtx/norm.hpp>

namespace vkh::hud {
Line::Line(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
           glm::vec3 color)
    : Element(view, parent, position, size), color{color} {};
void Line::addToDrawInfo(DrawInfo &drawInfo, float depth) {
  // TODO: this might be optimizable when nothing changes, maybe add a
  // "changed" flag
  uint32_t baseIndex =
      static_cast<uint32_t>(drawInfo.solidColorLineVertices.size());
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{position.x, position.y, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{position.x + size.x, position.y + size.y, depth}, color);

  drawInfo.solidColorLineIndices.push_back(baseIndex);
  drawInfo.solidColorLineIndices.push_back(baseIndex + 1);
};
bool Line::isPositionInside(const glm::vec2 &pos) {
  glm::vec2 position2 = position + size;
  float d1 = glm::distance(position, pos);
  float d2 = glm::distance(position2, pos);
  float d_line = glm::distance(position, position2);
  return glm::abs(d1 + d2 - d_line) <= .005f;
}
} // namespace vkh::hud
