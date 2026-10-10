#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <numeric>
#include <span>
#include <vector>

namespace UI::geometry {
inline float cross(glm::vec2 a, glm::vec2 b) { return a.x * b.y - a.y * b.x; }

inline bool contains(std::span<const glm::vec2> points, glm::vec2 p) {
  if (points.size() < 3)
    return false;
  bool inside = false;
  for (size_t i = 0, j = points.size() - 1; i < points.size(); j = i++) {
    const auto a = points[j], b = points[i], edge = b - a;
    if (std::abs(cross(edge, p - a)) < 1e-6f && glm::dot(p - a, p - b) <= 1e-6f)
      return true;
    if ((a.y > p.y) != (b.y > p.y) &&
        p.x < (b.x - a.x) * (p.y - a.y) / (b.y - a.y) + a.x)
      inside = !inside;
  }
  return inside;
}

// Ear clipping supports concave polygons and either winding. Coordinates
// stay local; moving/resizing a widget never changes its source geometry.
inline std::vector<uint32_t> triangulate(std::span<const glm::vec2> points) {
  if (points.size() < 3)
    return {};
  float area = 0;
  for (size_t i = 0; i < points.size(); ++i) {
    if (!std::isfinite(points[i].x) || !std::isfinite(points[i].y))
      return {};
    area += cross(points[i], points[(i + 1) % points.size()]);
  }
  if (std::abs(area) < 1e-7f)
    return {};
  const float winding = area > 0 ? 1.f : -1.f;
  std::vector<uint32_t> ring(points.size()), triangles;
  std::iota(ring.begin(), ring.end(), 0);
  while (ring.size() > 3) {
    bool clipped = false;
    for (size_t i = 0; i < ring.size(); ++i) {
      const auto a = ring[(i + ring.size() - 1) % ring.size()], b = ring[i],
                 c = ring[(i + 1) % ring.size()];
      if (cross(points[b] - points[a], points[c] - points[b]) * winding <=
          1e-7f)
        continue;
      bool occupied = false;
      for (auto j : ring) {
        if (j == a || j == b || j == c)
          continue;
        if (cross(points[b] - points[a], points[j] - points[a]) * winding >=
                -1e-7f &&
            cross(points[c] - points[b], points[j] - points[b]) * winding >=
                -1e-7f &&
            cross(points[a] - points[c], points[j] - points[c]) * winding >=
                -1e-7f) {
          occupied = true;
          break;
        }
      }
      if (occupied)
        continue;
      triangles.insert(triangles.end(), {a, b, c});
      ring.erase(ring.begin() + i);
      clipped = true;
      break;
    }
    if (!clipped)
      return {}; // degenerate/self-intersecting input
  }
  triangles.insert(triangles.end(), ring.begin(), ring.end());
  return triangles;
}
} // namespace UI::geometry
