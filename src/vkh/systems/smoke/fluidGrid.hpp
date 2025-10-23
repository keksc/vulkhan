#pragma once

#include <vector>

#include <glm/glm.hpp>

namespace vkh {
class FluidGrid {
public:
  std::vector<float> velocitiesX;
  std::vector<float> velocitiesY;
  std::vector<float> pressureMap;

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
  float getPressure(glm::ivec2 cell);
  float pressureSolveCell(glm::ivec2 cell);
  void solvePressure();

  const float density = 1;
  const float dt = 1.f / 60.f;

  void updateVelocities();

  std::vector<bool> solidCellMap;

  bool isSolid(glm::ivec2 cell);
  float sampleBilinear(const std::vector<float>& edgeValues,
                                glm::ivec2 size,
                                glm::vec2 worldPos) const;
  glm::vec2 getVelocityAtWorldPos(glm::vec2 worldPos);
};
} // namespace vkh
