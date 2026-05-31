#include "rectImg.hpp"

namespace vkh::hud {
RectImg::RectImg(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
           size_t imageIndex)
    : Element(view, parent, position, size), imageIndex{imageIndex} {};
void RectImg::addToDrawInfo(DrawInfo &drawInfo, float depth) {
  float x0 = absPos.x;
  float x1 = x0 + absSize.x;
  float y0 = absPos.y;
  float y1 = y0 + absSize.y;

  uint32_t baseIndex =
      static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());
  drawInfo.solidColorTriangleVertices.emplace_back(
      glm::vec3{x0, y0, depth}, glm::vec2{0.f, 0.f}, imageIndex);
  drawInfo.solidColorTriangleVertices.emplace_back(
      glm::vec3{x1, y0, depth}, glm::vec2{1.f, 0.f}, imageIndex);
  drawInfo.solidColorTriangleVertices.emplace_back(
      glm::vec3{x1, y1, depth}, glm::vec2{1.f, 1.f}, imageIndex);
  drawInfo.solidColorTriangleVertices.emplace_back(
      glm::vec3{x0, y1, depth}, glm::vec2{0.f, 1.f}, imageIndex);

  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 1);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
  drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 3);
};

} // namespace vkh::hud
