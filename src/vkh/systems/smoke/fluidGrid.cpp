#include "fluidGrid.hpp"

#include <random>

namespace vkh {
FluidGrid::FluidGrid(glm::ivec2 cellCount, float cellSize)
    : cellCount{cellCount}, cellSize{cellSize},
      velocitiesX((cellCount.x + 1) * cellCount.y),
      velocitiesY(cellCount.x * (cellCount.y + 1)) {
  std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<float> velocity(-1.f, 1.f);
  for (size_t x = 0; x < cellCount.x + 1; x++) {
    for (size_t y = 0; y < cellCount.y; y++) {
      velX(x, y) = velocity(rng);
    }
  }
  for (size_t x = 0; x < cellCount.x; x++) {
    for (size_t y = 0; y < cellCount.y + 1; y++) {
      velY(x, y) = velocity(rng);
    }
  }
}
float FluidGrid::calculateVelocityDivergence(glm::ivec2 cell) {
  float velocityTop = velY(cell.x, cell.y + 1);
  float velocityLeft = velX(cell.x, cell.y);
  float velocityRight = velX(cell.x + 1, cell.y);
  float velocityBottom = velY(cell.x, cell.y + 1);

  float gradientX = (velocityRight - velocityLeft) / cellSize;
  float gradientY = (velocityTop - velocityBottom) / cellSize;

  float divergence = gradientX + gradientY;
  return divergence;
}
} // namespace vkh
