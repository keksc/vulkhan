#pragma once

#include <glm/glm.hpp>
#include <vector>

struct Boid {
  glm::vec3 pos;
  glm::vec3 vel;
};

const float maxSpeed = 4.f;
const float maxForce =
    1.5f; // notably less than maxSpeed: smooth turning, not snapping
const float separationRange = 2.f; // tight personal-space bubble
const float alignmentRange = 6.f;
const float cohesionRange = 6.f;
const float idealDistanceToManor = 0.4f;

extern std::vector<Boid> boids;

void initBoids(int count);

void updateBoids(float dt);
