#include "emptyRect.hpp"

namespace vkh::hud {
EmptyRect::EmptyRect(View &view, Element *parent, glm::vec2 position,
                     glm::vec2 size)
    : Element(view, parent, position, size) {
}
void EmptyRect::addToDrawInfo(DrawInfo &drawInfo, float depth) {
  // TODO: this might be optimizable when nothing changes, maybe add a
  // "changed" flag
  glm::vec2 a = position;
  glm::vec2 b = glm::vec2{a.x + size.x, a.y};
  glm::vec2 c = glm::vec2{b.x, a.y + size.y};
  glm::vec2 d = glm::vec2{a.x, c.y};
  const glm::vec3 color{1.f};
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{a, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{b, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{b, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{c, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{c, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{d, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{d, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(
      glm::vec3{a, depth}, color);
};
} // namespace vkh::hud
