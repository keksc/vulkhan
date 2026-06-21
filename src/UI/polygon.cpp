#include "polygon.hpp"

#include <numeric>

namespace UI {
Polygon::Polygon(vkh::hud::View &view, Element *parent,
                 const std::vector<Vertex> &vertices, unsigned short imageIndex)
    : Element(view, parent, glm::vec2{}, glm::vec2{1.f}),
      imageIndex{imageIndex} {
  setVertices(vertices);
  update();
}

void Polygon::setVertices(const std::vector<Vertex> &newVertices) {
  vertices.clear();
  for (auto vtx : newVertices) // This performs a wanted copy
  {
    vtx.pos = absPos + vtx.pos * absSize;
    vertices.emplace_back(vtx);
  }
}

void Polygon::update() {
  if (vertices.empty()) {
    relPosition = glm::vec2{};
    relSize = glm::vec2{};
    updateAbsolute();
    return;
  }

  auto xCmp = [](const Vertex &e1, const Vertex &e2) {
    return e1.pos.x < e2.pos.x;
  };
  auto yCmp = [](const Vertex &e1, const Vertex &e2) {
    return e1.pos.y < e2.pos.y;
  };

  glm::vec2 minP{
      std::min_element(vertices.begin(), vertices.end(), xCmp)->pos.x,
      std::min_element(vertices.begin(), vertices.end(), yCmp)->pos.y};
  glm::vec2 maxP{
      std::max_element(vertices.begin(), vertices.end(), xCmp)->pos.x,
      std::max_element(vertices.begin(), vertices.end(), yCmp)->pos.y};

  glm::vec2 diff = maxP - minP;
  if (diff.x < 1e-6f)
    diff.x = 1e-6f;
  if (diff.y < 1e-6f)
    diff.y = 1e-6f;

  setAbsolutePosition(minP);
  setAbsoluteSize(diff);

  // Make vertices relative to position and size
  for (auto &v : this->vertices) {
    v.pos = (v.pos - minP) / diff;
  }

  g = std::accumulate(this->vertices.begin(), this->vertices.end(),
                      Vertex{.pos = glm::vec2{}, .uv = glm::vec2{}})
          .pos /
      static_cast<float>(this->vertices.size());
}
void Polygon::addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) {
  if (vertices.empty())
    return;

  uint32_t baseIndex =
      static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());

  glm::vec2 absG = absPos + g * absSize;
  drawInfo.solidColorTriangleVertices.emplace_back(
      glm::vec3{absG.x, absG.y, depth}, vertices[0].uv, imageIndex);

  for (size_t i = 0; i < vertices.size(); i++) {
    glm::vec2 absVPos = absPos + vertices[i].pos * absSize;
    drawInfo.solidColorTriangleVertices.emplace_back(
        glm::vec3{absVPos.x, absVPos.y, depth}, vertices[i].uv, imageIndex);
  }

  for (size_t i = 0; i < vertices.size(); i++) {
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + i + 1);
    drawInfo.solidColorTriangleIndices.emplace_back(
        baseIndex + (i + 1) % vertices.size() + 1);
  }
};

} // namespace UI
