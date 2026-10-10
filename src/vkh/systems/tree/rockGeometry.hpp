#pragma once
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <glm/glm.hpp>
#include <vector>

namespace vkh::rocks {
struct Vertex {
  glm::vec3 pos, normal;
};
struct Geometry {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
  glm::vec3 min{1e30f}, max{-1e30f};
};
// Broad correlated deformation and real silhouettes replace flat pebble
// stamps. Two subdivisions = 320 triangles, with no texture or asset bake.
inline Geometry generate(uint32_t seed, uint32_t subdivisions = 2) {
  const float t = (1.f + std::sqrt(5.f)) * 0.5f;
  const std::array<glm::vec3, 12> points{{{-1, t, 0},
                                          {1, t, 0},
                                          {-1, -t, 0},
                                          {1, -t, 0},
                                          {0, -1, t},
                                          {0, 1, t},
                                          {0, -1, -t},
                                          {0, 1, -t},
                                          {t, 0, -1},
                                          {t, 0, 1},
                                          {-t, 0, -1},
                                          {-t, 0, 1}}};
  const uint32_t faces[][3] = {{0, 11, 5},  {0, 5, 1},  {0, 1, 7},  {0, 7, 10},
                               {0, 10, 11}, {1, 5, 9},  {5, 11, 4}, {11, 10, 2},
                               {10, 7, 6},  {7, 1, 8},  {3, 9, 4},  {3, 4, 2},
                               {3, 2, 6},   {3, 6, 8},  {3, 8, 9},  {4, 9, 5},
                               {2, 4, 11},  {6, 2, 10}, {8, 6, 7},  {9, 8, 1}};
  using Triangle = std::array<glm::vec3, 3>;
  std::vector<Triangle> triangles;
  for (auto &f : faces)
    triangles.push_back({glm::normalize(points[f[0]]),
                         glm::normalize(points[f[1]]),
                         glm::normalize(points[f[2]])});
  for (uint32_t level = 0; level < std::min(subdivisions, 3u); ++level) {
    std::vector<Triangle> refined;
    refined.reserve(triangles.size() * 4);
    for (auto &v : triangles) {
      auto a = glm::normalize(v[0] + v[1]);
      auto b = glm::normalize(v[1] + v[2]);
      auto c = glm::normalize(v[2] + v[0]);
      refined.insert(refined.end(),
                     {{v[0], a, c}, {a, v[1], b}, {c, b, v[2]}, {a, b, c}});
    }
    triangles = std::move(refined);
  }
  const float phase = float(seed % 4096) * 0.731f;
  const glm::vec3 scale(1.0f + 0.25f * std::sin(phase),
                        0.62f + 0.15f * std::cos(phase * 1.7f),
                        0.85f + 0.18f * std::sin(phase * 0.8f));
  auto shape = [&](glm::vec3 n) {
    const float radius =
        1.f + 0.12f * std::sin(glm::dot(n, glm::vec3(3, 5, 2)) + phase) +
        0.055f * std::sin(glm::dot(n, glm::vec3(9, 3, 7)) - phase);
    glm::vec3 p = n * scale * radius;
    p.y = std::min(p.y, scale.y * (0.72f + p.x * 0.09f - p.z * 0.06f));
    return p;
  };
  Geometry result;
  for (auto &tri : triangles) {
    std::array<glm::vec3, 3> p{shape(tri[0]), shape(tri[1]), shape(tri[2])};
    glm::vec3 cross = glm::cross(p[1] - p[0], p[2] - p[0]);
    if (glm::dot(cross, cross) < 1e-10f)
      continue;
    const auto normal = glm::normalize(cross);
    for (int i = 0; i < 3; ++i) {
      const auto smooth = glm::normalize(tri[i] / scale);
      result.indices.push_back(static_cast<uint32_t>(result.vertices.size()));
      result.vertices.push_back(
          {p[i], glm::normalize(glm::mix(normal, smooth, 0.35f))});
      result.min = glm::min(result.min, p[i]);
      result.max = glm::max(result.max, p[i]);
    }
  }
  return result;
}
} // namespace vkh::rocks
