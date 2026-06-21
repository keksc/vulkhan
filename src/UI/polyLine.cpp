#include "polyLine.hpp"
#include <glm/gtx/norm.hpp>

namespace UI {
PolyLine::PolyLine(vkh::hud::View &view, Element *parent, glm::vec2 position,
                   glm::vec3 color)
    : Element(view, parent, position, glm::vec2{}), color{color} {
  points.push_back(position);
}

void PolyLine::addPoint(glm::vec2 point) {
  points.push_back(point);
  // Update bounding box
  glm::vec2 minP = points[0];
  glm::vec2 maxP = points[0];
  for (const auto &p : points) {
    minP = glm::min(minP, p);
    maxP = glm::max(maxP, p);
  }

  if (parent) {
    setAbsolutePosition(parent->getAbsolutePosition() +
                        minP * parent->getAbsoluteSize());
    setAbsoluteSize((maxP - minP) * parent->getAbsoluteSize());
  }
}

void PolyLine::addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) {
  if (points.size() < 2 || !parent)
    return;

  uint32_t baseIndex =
      static_cast<uint32_t>(drawInfo.solidColorLineVertices.size());

  for (size_t i = 0; i < points.size(); ++i) {
    glm::vec2 absPos =
        parent->getAbsolutePosition() + points[i] * parent->getAbsoluteSize();
    drawInfo.solidColorLineVertices.emplace_back(glm::vec3{absPos, depth},
                                                 color);
    if (i > 0) {
      drawInfo.solidColorLineIndices.push_back(baseIndex + i - 1);
      drawInfo.solidColorLineIndices.push_back(baseIndex + i);
    }
  }
}

bool PolyLine::isPositionInside(const glm::vec2 &pos) {
  if (!parent)
    return false;

  // First check bounding box for efficiency and to handle single-point cases
  const glm::vec2 minB = glm::min(absPos, absPos + absSize);
  const glm::vec2 maxB = glm::max(absPos, absPos + absSize);
  bool insideBox = glm::all(glm::greaterThanEqual(pos, minB)) &&
                   glm::all(glm::lessThanEqual(pos, maxB));

  if (!insideBox)
    return false;

  if (points.size() < 2)
    return true; // If it's just a point and we are inside the box, it's a hit

  for (size_t i = 0; i < points.size() - 1; ++i) {
    glm::vec2 p1 =
        parent->getAbsolutePosition() + points[i] * parent->getAbsoluteSize();
    glm::vec2 p2 = parent->getAbsolutePosition() +
                   points[i + 1] * parent->getAbsoluteSize();

    float d1 = glm::distance(p1, pos);
    float d2 = glm::distance(p2, pos);
    float d_line = glm::distance(p1, p2);
    if (glm::abs(d1 + d2 - d_line) <= .005f)
      return true;
  }
  return false;
}
} // namespace UI
