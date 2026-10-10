#include "polyLine.hpp"
#include "theme.hpp"
#include <glm/gtx/norm.hpp>

namespace UI {
PolyLine::PolyLine(vkh::hud::View &view, Element *parent, glm::vec2 position,
                   glm::vec3 color)
    : Element(view, parent, position, glm::vec2{}), color{color} {
  const uint32_t rgb = (uint32_t(glm::clamp(color.r, 0.f, 1.f) * 255) << 16) |
                       (uint32_t(glm::clamp(color.g, 0.f, 1.f) * 255) << 8) |
                       uint32_t(glm::clamp(color.b, 0.f, 1.f) * 255);
  inkTexture = theme::color(view, rgb);
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

  const glm::vec2 viewport = glm::vec2(view.context.window.size);
  if (viewport.x <= 0 || viewport.y <= 0)
    return;
  const glm::vec2 toPixels = viewport / 2.f;
  for (size_t i = 1; i < points.size(); ++i) {
    const glm::vec2 a = parent->getAbsolutePosition() +
                        points[i - 1] * parent->getAbsoluteSize();
    const glm::vec2 b =
        parent->getAbsolutePosition() + points[i] * parent->getAbsoluteSize();
    const glm::vec2 delta = (b - a) * toPixels;
    const float length = glm::length(delta);
    if (length < .001f)
      continue;
    const glm::vec2 normal = glm::vec2{-delta.y, delta.x} / length / toPixels;
    // Subtle pressure variation, tapered ends, and overlapping segments.
    const float pressure = .95f + .12f * std::sin(float(i) * .73f);
    const float width = 1.25f * view.layoutScale * pressure;
    const float startWidth = i == 1 ? width * .45f : width;
    const float endWidth = i + 1 == points.size() ? width * .45f : width;
    const uint32_t base = drawInfo.solidColorTriangleVertices.size();
    for (const auto &p : {a + normal * startWidth, a - normal * startWidth,
                          b - normal * endWidth, b + normal * endWidth})
      drawInfo.solidColorTriangleVertices.emplace_back(glm::vec3{p, depth},
                                                       glm::vec2{}, inkTexture);
    for (uint32_t offset : {0u, 1u, 2u, 0u, 2u, 3u})
      drawInfo.solidColorTriangleIndices.push_back(base + offset);
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
