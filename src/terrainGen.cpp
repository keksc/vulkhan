#include "terrainGen.hpp"

#include <fmt/core.h>

#include <random>
#include <vector>

std::vector<glm::ivec2> generateMansion() {
  std::random_device rd;
  std::mt19937 gen(rd());
  std::uniform_int_distribution<> distrib(0, 3);

  // West generation
  int N = 10;
  std::vector<glm::ivec2> tiles(N);
  glm::ivec2 cursor = {0, 0}; // where does west wing start ?
  for (int i = 0; i < N; i++) {
    switch (distrib(gen)) {
    case 0: // Left
      cursor.x--;
      break;
    case 1: // Right
      cursor.x++;
      break;
    case 2: // Forward
      cursor.y++;
      break;
    };
    tiles[i] = cursor;
  }
  return tiles;
}
