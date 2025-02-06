#include "terrainGen.hpp"

#include <fmt/core.h>

#include <glm/glm.hpp>
#include <random>

void generateMansion() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distrib(0, 3);

  // West generation
  glm::vec2 cursor = {0.f, 0.f}; // where does west wing start ?
  for (int i = 0; i < 10; i++)
    distrib(gen);
}
