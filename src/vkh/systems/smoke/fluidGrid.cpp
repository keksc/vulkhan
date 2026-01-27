#include "fluidGrid.hpp"

#include <random>

namespace vkh {
FluidGrid::FluidGrid(glm::ivec2 cellCount, float cellSize)
    : cellCount{cellCount}, cellSize{cellSize},
      velocitiesX((cellCount.x + 1) * cellCount.y),
      velocitiesY(cellCount.x * (cellCount.y + 1)),
      pressureMap(cellCount.x * cellCount.y),
      solidCellMap(cellCount.x * cellCount.y) {
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

  for (size_t x = 0; x < cellCount.x; x++) {
    solidCellMap[x] = true;
    solidCellMap[x + (cellCount.y - 1) * cellCount.x] = true;
  }
  for (size_t y = 0; y < cellCount.y; y++) {
    solidCellMap[y * cellCount.x] = true;
    solidCellMap[cellCount.x - 1 + y * cellCount.x] = true;
  }
}
float FluidGrid::calculateVelocityDivergence(glm::ivec2 cell) {
  float velocityTop = velY(cell.x, cell.y + 1);
  float velocityLeft = velX(cell.x, cell.y);
  float velocityRight = velX(cell.x + 1, cell.y);
  float velocityBottom = velY(cell.x, cell.y);

  float gradientX = (velocityRight - velocityLeft) / cellSize;
  float gradientY = (velocityTop - velocityBottom) / cellSize;

  float divergence = gradientX + gradientY;
  return divergence;
}
float FluidGrid::getPressure(glm::ivec2 cell) {
  bool outOfBounds = cell.x < 0 || cell.x >= cellCount.x || cell.y < 0 ||
                     cell.y >= cellCount.y;
  return outOfBounds ? 0.f : pressureMap[cell.x + cell.y * cellCount.x];
}
float FluidGrid::pressureSolveCell(glm::ivec2 cell) {
  int flowTop = isSolid({cell.x, cell.y + 1}) ? 0 : 1;
  int flowLeft = isSolid({cell.x - 1, cell.y}) ? 0 : 1;
  int flowRight = isSolid({cell.x + 1, cell.y}) ? 0 : 1;
  int flowBottom = isSolid({cell.x, cell.y - 1}) ? 0 : 1;
  int fluidEdgeCount = flowTop + flowLeft + flowRight + flowBottom;

  if (isSolid(cell) || !fluidEdgeCount)
    return 0.f;

  float pressureTop = getPressure({cell.x, cell.y + 1}) * flowTop;
  float pressureLeft = getPressure({cell.x - 1, cell.y}) * flowLeft;
  float pressureRight = getPressure({cell.x + 1, cell.y}) * flowRight;
  float pressureBottom = getPressure({cell.x, cell.y - 1}) * flowBottom;
  float velocityTop = velY(cell.x, cell.y + 1) * flowTop;
  float velocityLeft = velX(cell.x, cell.y) * flowLeft;
  float velocityRight = velX(cell.x + 1, cell.y) * flowRight;
  float velocityBottom = velY(cell.x, cell.y) * flowBottom;

  float pressureSum =
      pressureTop + pressureRight + pressureLeft + pressureBottom;
  float deltaVelocitySum =
      velocityRight - velocityLeft + velocityTop - velocityBottom;
  return (pressureSum - density * cellSize * deltaVelocitySum / dt) /
         fluidEdgeCount;
}
void FluidGrid::solvePressure() {
  const int iterations = 50; // Tune based on grid size; 20–100 for convergence
  std::vector<float> tempPressure(cellCount.x * cellCount.y);
  for (int iter = 0; iter < iterations; ++iter) {
    for (size_t x = 0; x < cellCount.x; x++) {
      for (size_t y = 0; y < cellCount.y; y++) {
        tempPressure[x + y * cellCount.x] = pressureSolveCell({x, y});
      }
    }
    pressureMap = tempPressure; // Swap for next iteration
  }
}
void FluidGrid::updateVelocities() {
  const float K = dt / (density * cellSize);

  for (size_t x = 0; x < cellCount.x + 1; x++) {
    for (size_t y = 0; y < cellCount.y; y++) {
      if (isSolid({x, y}) || isSolid({x - 1, y})) {
        velX(x, y) = 0.f;
        continue;
      }
      float pressureRight = getPressure({x, y});
      float pressureLeft = getPressure({x - 1, y});
      velX(x, y) -= K * (pressureRight - pressureLeft);
    }
  }

  for (size_t x = 0; x < cellCount.x; x++) {
    for (size_t y = 0; y < cellCount.y + 1; y++) {
      if (isSolid({x, y}) || isSolid({x, y - 1})) {
        velY(x, y) = 0.f;
        continue;
      }
      float pressureTop = getPressure({x, y});
      float pressureBottom = getPressure({x, y - 1});
      velY(x, y) -= K * (pressureTop - pressureBottom);
    }
  }
}
bool FluidGrid::isSolid(glm::ivec2 cell) {
  bool outOfBounds = cell.x < 0 || cell.x >= cellCount.x || cell.y < 0 ||
                     cell.y >= cellCount.y;
  return outOfBounds ? true : solidCellMap[cell.x + cell.y * cellCount.x];
}
glm::vec2 FluidGrid::getVelocityAtWorldPos(glm::vec2 worldPos) {
  int left = static_cast<int>(worldPos.x);
  int top = static_cast<int>(worldPos.x);
  int right = left + 1;
  int bottom = top + 1;

  float px = worldPos.x - left;
  float py = worldPos.y - top;

  glm::vec2 value{};
  {
    float valueTop = glm::mix(velocitiesX[left * cellCount.x + top],
                              velocitiesX[right * cellCount.x + top], px);
    float valueBottom = glm::mix(velocitiesX[left * cellCount.x + bottom],
                                 velocitiesX[right * cellCount.x + bottom], px);
    value.x = glm::mix(valueTop, valueBottom, py);
  }
  {
    float valueLeft = glm::mix(velocitiesX[top * cellCount.y + left],
                               velocitiesX[bottom * cellCount.y + left], px);
    float valueRight = glm::mix(velocitiesX[top * cellCount.x + right],
                                velocitiesX[bottom * cellCount.y + right], px);
    value.y = glm::mix(valueLeft, valueRight, px);
  }
  return value;
}
} // namespace vkh
