#pragma once

#include <glm/glm.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <vector>

namespace vkh {

// std430. Three boundary guides over four root-to-tip layers form a
// triangular surface patch, rather than the old one-dimensional card strip.
// position.w = coat V; normal.w = coat U. Skinning restores position W=1.
struct alignas(16) FurGuide {
  glm::vec4 position{}, normal{};
  glm::uvec4 joints{};
  glm::vec4 weights{};
};
struct alignas(16) FurBundle {
  std::array<FurGuide, 12> guides;
  glm::vec4 parameters{}; // expected strand count, mean length, reserved
};
static_assert(sizeof(FurGuide) == 64 && sizeof(FurBundle) == 784);

// Hairless skin + Blender-authored per-vertex grooming data. Root triangles
// cover the surface without gaps; GPU sampling is uniform over their area.
// Each guide retains its own body skin weights and coat UV coordinates.
template <typename Vertex>
std::vector<FurBundle>
buildSurfaceFurGroom(std::span<const Vertex> vertices,
                     std::span<const uint32_t> indices,
                     std::span<const glm::vec3> parameters,
                     std::span<const glm::vec3> directions) {
  if (indices.empty() || indices.size() % 3 != 0)
    throw std::runtime_error("Fur emitters must be indexed skin triangles");
  auto finite = [](glm::vec3 v) {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
  };
  struct Emitter {
    Vertex vertex;
    glm::vec3 parameters, flow;
  };
  std::vector<FurBundle> result;
  result.reserve(indices.size() / 3);
  // Density is per square metre, so retopology does not change coverage.
  constexpr float strandsPerSquareMetre = 45000.f;
  auto emit = [&](auto &&self, const std::array<Emitter, 3> &e,
                  uint32_t depth) -> void {
    const auto &a = e[0].vertex.pos;
    const auto &b = e[1].vertex.pos;
    const auto &c = e[2].vertex.pos;
    const float area = .5f * glm::length(glm::cross(b - a, c - a));
    // Keep any triangle touching a bare vertex hairless. This gives eyes,
    // nose, mouth and claws a clean edge instead of letting interpolated
    // strands leak across the feature boundary.
    const float density = std::min({e[0].parameters.y, e[1].parameters.y,
                                    e[2].parameters.y});
    const float count = area * density * strandsPerSquareMetre;
    if (count <= 0 || area < 1e-12f)
      return;
    if (count > 64.f) {
      if (depth >= 16)
        throw std::runtime_error("Fur emitter triangle is too large");
      // Split the longest edge. Interpolate the same four-weight skinning
      // contract as glTF, including vertices spanning different joints.
      size_t edge = 0;
      float longest = 0;
      for (size_t i = 0; i < 3; ++i) {
        const auto delta = e[(i + 1) % 3].vertex.pos - e[i].vertex.pos;
        if (glm::dot(delta, delta) > longest) {
          longest = glm::dot(delta, delta);
          edge = i;
        }
      }
      const auto &x = e[edge];
      const auto &y = e[(edge + 1) % 3];
      Emitter m{};
      m.vertex.pos = (x.vertex.pos + y.vertex.pos) * .5f;
      m.vertex.normal = glm::normalize(x.vertex.normal + y.vertex.normal);
      m.vertex.uv = (x.vertex.uv + y.vertex.uv) * .5f;
      std::array<std::pair<uint32_t, float>, 8> weights{};
      size_t n = 0;
      for (const auto *v : {&x.vertex, &y.vertex})
        for (size_t j = 0; j < 4; ++j) {
          const uint32_t joint = v->jointIndices[j];
          size_t k = 0;
          while (k < n && weights[k].first != joint)
            ++k;
          if (k == n)
            weights[n++] = {joint, 0};
          weights[k].second += v->jointWeights[j] * .5f;
        }
      std::sort(weights.begin(), weights.end(),
                [](auto x, auto y) { return x.second > y.second; });
      float sum = 0;
      for (size_t j = 0; j < 4; ++j) {
        m.vertex.jointIndices[j] = weights[j].first;
        m.vertex.jointWeights[j] = weights[j].second;
        sum += weights[j].second;
      }
      if (sum > 0)
        m.vertex.jointWeights /= sum;
      m.parameters = (x.parameters + y.parameters) * .5f;
      m.flow = (x.flow + y.flow) * .5f;
      auto left = e, right = e;
      left[(edge + 1) % 3] = m;
      right[edge] = m;
      self(self, left, depth + 1);
      self(self, right, depth + 1);
      return;
    }
    FurBundle bundle{};
    bundle.parameters = {
        count,
        (e[0].parameters.x + e[1].parameters.x + e[2].parameters.x) / 3.f, 0,
        0};
    for (size_t corner = 0; corner < 3; ++corner) {
      const auto &v = e[corner].vertex;
      const auto p = e[corner].parameters;
      const glm::vec3 normal = glm::normalize(v.normal);
      glm::vec3 tangent =
          e[corner].flow - normal * glm::dot(e[corner].flow, normal);
      if (glm::length(tangent) < 1e-5f)
        tangent =
            glm::cross(normal, std::abs(normal.y) < .9f ? glm::vec3(0, 1, 0)
                                                        : glm::vec3(1, 0, 0));
      tangent = glm::normalize(tangent);
      for (size_t layer = 0; layer < 4; ++layer) {
        const float t = float(layer) / 3.f;
        const auto position = v.pos + p.x * (normal * (p.z * t - .12f * t * t) +
                                             tangent * (.4f * t + .6f * t * t));
        bundle.guides[layer * 3 + corner] = {glm::vec4(position, v.uv.y),
                                             glm::vec4(normal, v.uv.x),
                                             v.jointIndices, v.jointWeights};
      }
    }
    result.push_back(bundle);
  };
  for (size_t i = 0; i < indices.size(); i += 3) {
    std::array<Emitter, 3> e;
    for (size_t j = 0; j < 3; ++j) {
      const size_t index = indices[i + j];
      if (index >= vertices.size() || index >= parameters.size() ||
          index >= directions.size())
        throw std::runtime_error("Fur emitter index out of bounds");
      const auto &v = vertices[index];
      const auto p = parameters[index];
      if (!finite(v.pos) || !finite(v.normal) ||
          glm::length(v.normal) < 1e-5f || !finite(p) ||
          !finite(directions[index]) || p.x < 0 || p.x > 1 || p.y < 0 ||
          p.y > 1 || p.z < 0 || p.z > 1 || !std::isfinite(v.uv.x) ||
          !std::isfinite(v.uv.y))
        throw std::runtime_error("Invalid surface fur values");
      e[j] = {v, p, directions[index]};
    }
    emit(emit, e, 0);
  }
  return result;
}

} // namespace vkh
