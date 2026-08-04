#pragma once

#include <functional>
#include <glm/glm.hpp>

namespace sdf {

class Sdf {
public:
  Sdf() = default;
  Sdf(std::function<float(glm::vec3)> fn) : fn_(std::move(fn)) {}

  float operator()(glm::vec3 p) const { return fn_(p); }

  explicit operator bool() const { return static_cast<bool>(fn_); }

private:
  std::function<float(glm::vec3)> fn_;
};

// --- operators: union (|), intersect (&), subtract (-) ---
inline Sdf operator|(const Sdf &a, const Sdf &b) {
  return Sdf([a, b](glm::vec3 p) { return glm::min(a(p), b(p)); });
}

inline Sdf operator&(const Sdf &a, const Sdf &b) {
  return Sdf([a, b](glm::vec3 p) { return glm::max(a(p), b(p)); });
}

// a - b: subtracts b's volume from a
inline Sdf operator-(const Sdf &a, const Sdf &b) {
  return Sdf([a, b](glm::vec3 p) { return glm::max(-b(p), a(p)); });
}

// --- primitives ---
inline Sdf box(glm::vec3 center, glm::vec3 halfExtents) {
  return Sdf([center, halfExtents](glm::vec3 p) {
    glm::vec3 d = glm::abs(p - center) - halfExtents;
    return glm::length(glm::max(d, glm::vec3(0.0f))) +
           glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f);
  });
}

inline Sdf sphere(glm::vec3 center, float radius) {
  return Sdf([center, radius](glm::vec3 p) {
    return glm::length(p - center) - radius;
  });
}

inline Sdf roundedBox(glm::vec3 center, glm::vec3 halfExtents, float rounding) {
  return Sdf([center, halfExtents, rounding](glm::vec3 p) {
    glm::vec3 d = glm::abs(p - center) - halfExtents + glm::vec3(rounding);
    return glm::length(glm::max(d, glm::vec3(0.0f))) +
           glm::min(glm::max(d.x, glm::max(d.y, d.z)), 0.0f) - rounding;
  });
}

inline Sdf capsule(glm::vec3 a, glm::vec3 b, float r) {
  return Sdf([a, b, r](glm::vec3 p) {
    glm::vec3 pa = p - a, ba = b - a;
    float h = glm::clamp(glm::dot(pa, ba) / glm::dot(ba, ba), 0.0f, 1.0f);
    return glm::length(pa - ba * h) - r;
  });
}

inline Sdf smoothUnion(const Sdf &a, const Sdf &b, float k) {
  return Sdf([a, b, k](glm::vec3 p) {
    float da = a(p), db = b(p);
    float h = glm::clamp(0.5f + 0.5f * (db - da) / k, 0.0f, 1.0f);
    return glm::mix(db, da, h) - k * h * (1.0f - h);
  });
}

// central-difference gradient (outward normal at/near the surface)
inline glm::vec3 gradient(const Sdf &fn, glm::vec3 p, float eps = 0.001f) {
  return glm::normalize(
      glm::vec3(fn(p + glm::vec3(eps, 0, 0)) - fn(p - glm::vec3(eps, 0, 0)),
                fn(p + glm::vec3(0, eps, 0)) - fn(p - glm::vec3(0, eps, 0)),
                fn(p + glm::vec3(0, 0, eps)) - fn(p - glm::vec3(0, 0, eps))));
}

} // namespace sdf
