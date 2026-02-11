#pragma once

#include <glm/glm.hpp>
#include <vector>

namespace vkh {
class FluidGrid {
public:
  std::vector<float> velocitiesX;
  std::vector<float> velocitiesY;
  std::vector<float> prevVelocitiesX;
  std::vector<float> prevVelocitiesY;

  std::vector<float> pressureMap;
  std::vector<float> tempPressure;

  std::vector<float> smokeMap;
  std::vector<float> targetSmoke;

  float cellSize;
  glm::ivec2 cellCount;

  const float density = 1.f;
  const float dt = 1.f / 60.f;

  FluidGrid(glm::ivec2 cellCount, float cellSize);

  inline float &velX(int i, int j) { return velocitiesX[i * cellCount.y + j]; }
  inline const float &velX(int i, int j) const {
    return velocitiesX[i * cellCount.y + j];
  }

  inline float &velY(int i, int j) {
    return velocitiesY[i * (cellCount.y + 1) + j];
  }
  inline const float &velY(int i, int j) const {
    return velocitiesY[i * (cellCount.y + 1) + j];
  }

  inline float &smoke(int i, int j) { return smokeMap[i * cellCount.y + j]; }
  inline const float &smoke(int i, int j) const {
    return smokeMap[i * cellCount.y + j];
  }

  void solvePressure();
  void updateVelocities();
  void advectVelocities();
  void advectSmoke();

  std::vector<bool> solidCellMap;
  bool isSolid(glm::ivec2 cell);
  float calculateVelocityDivergence(glm::ivec2 cell);

  // Sampling / Interpolation
  glm::vec2 getVelocityAtWorldPos(glm::vec2 worldPos);
  float getSmokeAtWorldPos(glm::vec2 worldPos);

private:
  float getPressure(glm::ivec2 cell);
  float pressureSolveCell(glm::ivec2 cell);

  // Generic bilinear sampler
  float sampleField(const std::vector<float> &field, float x, float y,
                    int strideY, int boundX, int boundY);
};
} // namespace vkh
