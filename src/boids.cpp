#include "boids.hpp"

#include <glm/gtc/random.hpp>
#include <glm/gtx/optimum_pow.hpp>

#include "vkh/AABB.hpp"

#include <limits>

std::vector<Boid> boids;
std::vector<vkh::AABB> manorAABBs = {
    {
        glm::vec3{-8.f, -3.f, -5.f},
        glm::vec3{8.f, 3.f, 5.f},
    },
    {
        glm::vec3{7.f, 0.f, -3.f},
        glm::vec3{13.f, 5.f, 9.f},
    },
    {
        glm::vec3{-11.f, 0.f, -6.f},
        glm::vec3{7.f, 10.f, -2.f},
    },
};

namespace {
glm::vec3 clampLength(const glm::vec3 &v, float maxLen) {
  float len = glm::length(v);
  if (len > maxLen && len > 0.0f) {
    return v * (maxLen / len);
  }
  return v;
}
} // namespace

glm::vec3 getClosestPointOnManor(glm::vec3 p) {
  glm::vec3 closest = glm::clamp(p, manorAABBs[0].min, manorAABBs[0].max);
  float dist2 = glm::length2(p - closest);
  for (size_t i = 1; i < manorAABBs.size(); i++) {
    glm::vec3 newClosest = glm::clamp(p, manorAABBs[i].min, manorAABBs[i].max);
    float newDist2 = glm::length2(p - newClosest);
    if (newDist2 < dist2) {
      closest = newClosest;
      dist2 = newDist2;
    }
  }
  return closest;
}

glm::vec3 separationForce(const std::vector<Boid> &boids, Boid &b) {
  glm::vec3 total{};
  for (const Boid &o : boids) {
    glm::vec3 v = b.pos - o.pos;
    float l2 = glm::length2(v);
    if (&o == &b || l2 <= 1e-8f || l2 > glm::pow2(separationRange))
      continue;
    total += v / l2;
  }
  if (glm::length2(total) <= 1e-8f)
    return glm::vec3{};
  glm::vec3 steering = glm::normalize(total) * maxSpeed - b.vel;
  steering = clampLength(steering, maxForce);
  return steering;
}

glm::vec3 alignmentForce(const std::vector<Boid> &boids, Boid &b) {
  glm::vec3 total{};
  size_t count{};
  for (const Boid &o : boids) {
    glm::vec3 v = b.pos - o.pos;
    float l2 = glm::length2(v);
    if (&o == &b || l2 > glm::pow2(alignmentRange))
      continue;
    total += o.vel;
    count++;
  }
  if (count == 0)
    return glm::vec3{0.f};
  glm::vec3 steering = (total / static_cast<float>(count)) - b.vel;
  steering = clampLength(steering, maxForce);
  return steering;
}

glm::vec3 cohesionForce(const std::vector<Boid> &boids, Boid &b) {
  glm::vec3 total{};
  size_t count{};
  for (const Boid &o : boids) {
    glm::vec3 v = b.pos - o.pos;
    float l2 = glm::length2(v);
    if (&o == &b || l2 > glm::pow2(cohesionRange))
      continue;
    total += o.pos;
    count++;
  }
  if (count == 0)
    return glm::vec3{0.f};
  glm::vec3 v = total / static_cast<float>(count) - b.pos;
  if (glm::length2(v) <= 1e-8f)
    return glm::vec3{0.f};
  glm::vec3 steering = glm::normalize(v) * maxSpeed - b.vel;
  steering = clampLength(steering, maxForce);
  return steering;
}

glm::vec3 manorForce(Boid &b) {
  const float manorOrbitDistance = 3.f;

  glm::vec3 closest = getClosestPointOnManor(b.pos);
  glm::vec3 outward = b.pos - closest;
  float dist2 = glm::length2(outward);

  glm::vec3 outDir;
  float dist;
  if (dist2 <= 1e-8f) {
    outDir = glm::vec3(0.f, 1.f, 0.f);
    dist = 0.f;
  } else {
    dist = glm::sqrt(dist2);
    outDir = outward / dist;
  }

  float error = dist - manorOrbitDistance;

  glm::vec3 desiredVel = -outDir * error;
  desiredVel = clampLength(desiredVel, maxSpeed);

  glm::vec3 steering = desiredVel - b.vel;
  steering = clampLength(steering, maxForce);
  return steering;
}

glm::vec3 manorTangentForce(Boid &b) {
  const float tangentSpeed = 3.f; // desired circling speed

  glm::vec3 closest = getClosestPointOnManor(b.pos);
  glm::vec3 outward = b.pos - closest;
  float dist2 = glm::length2(outward);

  if (dist2 <= 1e-8f) {
    return glm::vec3{0.f};
  }

  glm::vec3 outDir = outward / glm::sqrt(dist2);

  const glm::vec3 rotationDirnDir{0.f, 1.f, 0.f}; // rotation direction
  glm::vec3 tangent = glm::cross(outDir, rotationDirnDir);
  float tangentLen2 = glm::length2(tangent);

  if (tangentLen2 <= 1e-8f) {
    return glm::vec3{0.f};
  }

  tangent /= glm::sqrt(tangentLen2);

  glm::vec3 desiredVel = tangent * tangentSpeed;
  glm::vec3 steering = desiredVel - b.vel;
  steering = clampLength(steering, maxForce);
  return steering;
}

void initBoids(int count) {
  boids.clear();
  boids.reserve(static_cast<size_t>(count));

  vkh::AABB bounds{glm::vec3{-10.f}, glm::vec3{10.f}};

  glm::vec3 direction = glm::sphericalRand(1.0f);

  for (int i = 0; i < count; i++) {
    Boid b;
    b.pos = glm::linearRand(bounds.min, bounds.max);
    b.vel = glm::sphericalRand(1.0f);
    boids.push_back(b);
  }
}

void updateBoids(float dt) {
  std::vector<Boid> old = boids;
  for (size_t i = 0; i < old.size(); i++) {
    glm::vec3 accel{};
    accel += separationForce(old, old[i]);
    accel += alignmentForce(old, old[i]);
    accel += cohesionForce(old, old[i]);
    accel += manorForce(old[i]);
    accel += manorTangentForce(old[i]);
    boids[i].vel += accel * dt;
    boids[i].vel = clampLength(boids[i].vel, maxSpeed);
    boids[i].pos += boids[i].vel * dt;
  }
}
