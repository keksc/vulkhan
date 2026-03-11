#include "emptyRect.hpp"

namespace vkh::hud {
EmptyRect::EmptyRect(View &view, Element *parent, glm::vec2 position,
                     glm::vec2 size)
    : Element(view, parent, position, size) {}
void EmptyRect::addToDrawInfo(DrawInfo &drawInfo, float depth) {
  uint32_t baseIndex =
      static_cast<uint32_t>(drawInfo.solidColorLineVertices.size());

  // Define 4 vertices
  glm::vec2 a = position;
  glm::vec2 b = {a.x + size.x, a.y};
  glm::vec2 c = {b.x, a.y + size.y};
  glm::vec2 d = {a.x, c.y};
  const glm::vec3 color{1.f};

  drawInfo.solidColorLineVertices.emplace_back(glm::vec3{a, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(glm::vec3{b, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(glm::vec3{c, depth}, color);
  drawInfo.solidColorLineVertices.emplace_back(glm::vec3{d, depth}, color);

  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 0);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 1);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 1);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 2);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 2);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 3);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 3);
  drawInfo.solidColorLineIndices.emplace_back(baseIndex + 0);
}
} // namespace vkh::hud
