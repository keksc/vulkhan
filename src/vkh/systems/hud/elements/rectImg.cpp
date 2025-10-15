#include "rectImg.hpp"

namespace vkh::hud {
Rect::Rect(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
           unsigned short imageIndex)
    : Element(view, parent, position, size), imageIndex{imageIndex} {};
void Rect::addToDrawInfo(DrawInfo &drawInfo, float depth) {
  // TODO: this might be optimizable when nothing changes, maybe add a
  // "changed" flag
  float x0 = position.x;
  float x1 = x0 + size.x;
  float y0 = position.y;
  float y1 = y0 + size.y;

  uint32_t baseIndex =
      static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());
  drawInfo.solidColorTriangleVertices.emplace_back(glm::vec3{x0, y0, depth},
                                                   glm::vec2{0.f, 0.f});
  drawInfo.solidColorTriangleVertices.emplace_back(glm::vec3{x1, y0, depth},
                                                   glm::vec2{1.f, 0.f});
  drawInfo.solidColorTriangleVertices.emplace_back(glm::vec3{x1, y1, depth},
                                                   glm::vec2{1.f, 1.f});
  drawInfo.solidColorTriangleVertices.emplace_back(glm::vec3{x0, y1, depth},
                                                   glm::vec2{0.f, 1.f});

  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 1);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 3);
};

} // namespace vkh::hud
