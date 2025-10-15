#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace vkh {
class FluidGrid {
public:
  std::vector<float> velocitiesX;
  std::vector<float> velocitiesY;

  float cellSize;

  glm::ivec2 cellCount;
  FluidGrid(glm::ivec2 cellCount, float cellSize);

  inline float &velX(int i, int j) { return velocitiesX[i * cellCount.y + j]; }

  inline float &velY(int i, int j) {
    return velocitiesY[i * (cellCount.y + 1) + j];
  }

  inline const float &velX(int i, int j) const {
    return velocitiesX[i * cellCount.y + j];
  }

  inline const float &velY(int i, int j) const {
    return velocitiesY[i * (cellCount.y + 1) + j];
  }
  float calculateVelocityDivergence(glm::ivec2 cell);
};
} // namespace vkh
