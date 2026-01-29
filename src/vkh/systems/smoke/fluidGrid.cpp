#include "fluidGrid.hpp"

#include <glm/ext/vector_common.hpp>
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
void FluidGrid::advectVelocities() {
  std::vector<float> nextVelX = velocitiesX;
  std::vector<float> nextVelY = velocitiesY;

  for (size_t x = 0; x < cellCount.x + 1; x++) {
    for (size_t y = 0; y < cellCount.y; y++) {
      glm::vec2 pos(static_cast<float>(x), static_cast<float>(y) + 0.5f);

      glm::vec2 vel = getVelocityAtWorldPos(pos);

      glm::vec2 prevPos = pos - vel * dt;

      nextVelX[x * cellCount.y + y] = getVelocityAtWorldPos(prevPos).x;
    }
  }

  for (size_t x = 0; x < cellCount.x; x++) {
    for (size_t y = 0; y < cellCount.y + 1; y++) {
      glm::vec2 pos(static_cast<float>(x) + 0.5f, static_cast<float>(y));

      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 prevPos = pos - vel * dt;

      nextVelY[x * (cellCount.y + 1) + y] = getVelocityAtWorldPos(prevPos).y;
    }
  }

  velocitiesX = std::move(nextVelX);
  velocitiesY = std::move(nextVelY);
}
bool FluidGrid::isSolid(glm::ivec2 cell) {
  bool outOfBounds = cell.x < 0 || cell.x >= cellCount.x || cell.y < 0 ||
                     cell.y >= cellCount.y;
  return outOfBounds ? true : solidCellMap[cell.x + cell.y * cellCount.x];
}
glm::vec2 FluidGrid::getVelocityAtWorldPos(glm::vec2 worldPos) {
  float uX =
      glm::clamp(worldPos.x, 0.f, (float)cellCount.x - 0.001f); // Added -0.001f
  float uY = glm::clamp(worldPos.y - 0.5f, 0.f, (float)cellCount.y - 1.001f);

  // For V: shift X by 0.5
  float vX = glm::clamp(worldPos.x - 0.5f, 0.f, (float)cellCount.x - 1.001f);
  float vY =
      glm::clamp(worldPos.y, 0.f, (float)cellCount.y - 0.001f); // Added -0.001f

  // Interpolate U
  int iU = static_cast<int>(uX);
  int jU = static_cast<int>(uY);
  float txU = uX - iU;
  float tyU = uY - jU;
  float vx =
      glm::mix(glm::mix(velX(iU, jU), velX(iU + 1, jU), txU),
               glm::mix(velX(iU, jU + 1), velX(iU + 1, jU + 1), txU), tyU);

  // Interpolate V
  int iV = static_cast<int>(vX);
  int jV = static_cast<int>(vY);
  float txV = vX - iV;
  float tyV = vY - jV;
  float vy =
      glm::mix(glm::mix(velY(iV, jV), velY(iV + 1, jV), txV),
               glm::mix(velY(iV, jV + 1), velY(iV + 1, jV + 1), txV), tyV);

  return {vx, vy};
}
} // namespace vkh
