#include "polygon.hpp"
#include "polygonGeometry.hpp"

namespace UI {
Polygon::Polygon(vkh::hud::View &view, Element *parent,
                 const std::vector<Vertex> &vertices, unsigned short imageIndex)
    : Element(view, parent, glm::vec2{}, glm::vec2{1.f}),
      imageIndex(imageIndex) {
  setVertices(vertices);
}
void Polygon::setVertices(const std::vector<Vertex> &newVertices) {
  vertices = newVertices;
  update();
}
void Polygon::update() {
  std::vector<glm::vec2> positions;
  for (const auto &vertex : vertices)
    positions.push_back(vertex.pos);
  indices = geometry::triangulate(positions);
}
bool Polygon::isPositionInside(const glm::vec2 &p) {
  if (indices.empty() || std::abs(absSize.x) < 1e-6f ||
      std::abs(absSize.y) < 1e-6f)
    return false;
  std::vector<glm::vec2> positions;
  for (const auto &vertex : vertices)
    positions.push_back(vertex.pos);
  return geometry::contains(positions, (p - absPos) / absSize);
}
void Polygon::addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) {
  if (indices.empty())
    return;
  const uint32_t first = drawInfo.solidColorTriangleVertices.size();
  for (const auto &vertex : vertices) {
    const auto position = absPos + vertex.pos * absSize;
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec3(position, depth),
                                                     vertex.uv, imageIndex);
  }
  for (auto i : indices)
    drawInfo.solidColorTriangleIndices.push_back(first + i);
}
} // namespace UI
