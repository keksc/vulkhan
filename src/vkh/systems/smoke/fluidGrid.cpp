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
/*
 * Bilinearly interpolate a scalar field that lives on *edges*.
 *
 * @param edgeValues  1‑D vector that stores the field in row‑major order.
 *                    Size must be size.x * size.y.
 * @param size        Number of *edges* in (x,y). For velX this is
 *                    {cellCount.x+1, cellCount.y},
 *                    for velY it is {cellCount.x, cellCount.y+1}.
 * @param worldPos    Position in world space (origin at the centre of the
 *                    whole domain).
 *
 * The domain spans  [-width/2 , +width/2)  x  [-height/2 , +height/2)
 * where width  = size.x * cellSize
 *       height = size.y * cellSize
 *
 * The function clamps to the valid edge range, so out‑of‑bounds
 * positions return the nearest edge value.
 */
float FluidGrid::sampleBilinear(const std::vector<float>& edgeValues,
                                glm::ivec2 size,
                                glm::vec2 worldPos) const
{
    // ---- 1. Compute total extent of the edge grid -----------------
    const float width  = size.x * cellSize;   // total width  of the edge field
    const float height = size.y * cellSize;   // total height of the edge field

    // ---- 2. Map world → edge‑index space (float) -----------------
    //   centre of the domain → (0,0) in edge‑index space
    const float px = (worldPos.x + width  / 2.0f) / cellSize; // [0, size.x)
    const float py = (worldPos.y + height / 2.0f) / cellSize; // [0, size.y)

    // ---- 3. Clamp to the *inner* cell (so we always have 4 neighbours) --
    const int ix0 = static_cast<int>(glm::clamp(px, 0.0f, static_cast<float>(size.x - 2)));
    const int iy0 = static_cast<int>(glm::clamp(py, 0.0f, static_cast<float>(size.y - 2)));
    const int ix1 = ix0 + 1;
    const int iy1 = iy0 + 1;

    // ---- 4. Fractional part inside the cell -----------------------
    const float fracX = glm::clamp(px - static_cast<float>(ix0), 0.0f, 1.0f);
    const float fracY = glm::clamp(py - static_cast<float>(iy0), 0.0f, 1.0f);

    // ---- 5. Linear index into the 1‑D vector -----------------------
    const auto idx = [size](int x, int y) { return x + y * size.x; };

    const float v00 = edgeValues[idx(ix0, iy0)];   // bottom‑left
    const float v10 = edgeValues[idx(ix1, iy0)];   // bottom‑right
    const float v01 = edgeValues[idx(ix0, iy1)];   // top‑left
    const float v11 = edgeValues[idx(ix1, iy1)];   // top‑right

    // ---- 6. Bilinear interpolation -------------------------------
    const float bottom = glm::mix(v00, v10, fracX);
    const float top    = glm::mix(v01, v11, fracX);
    return glm::mix(bottom, top, fracY);
}
glm::vec2 FluidGrid::getVelocityAtWorldPos(glm::vec2 worldPos) {
  return {
      sampleBilinear(velocitiesX, {cellCount.x + 1, cellCount.y}, worldPos),
      sampleBilinear(velocitiesY, {cellCount.x, cellCount.y + 1}, worldPos)};
}
} // namespace vkh
