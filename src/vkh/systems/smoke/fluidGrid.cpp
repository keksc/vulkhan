#include "fluidGrid.hpp"

#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <algorithm>
#include <random>

namespace vkh {

FluidGrid::FluidGrid(glm::ivec2 cellCount, float cellSize)
    : cellCount{cellCount}, cellSize{cellSize},
      velocitiesX((cellCount.x + 1) * cellCount.y),
      velocitiesY(cellCount.x * (cellCount.y + 1)),
      pressureMap(cellCount.x * cellCount.y),
      smokeMap(cellCount.x * cellCount.y),
      solidCellMap(cellCount.x * cellCount.y),
      // Initialize scratch buffers
      prevVelocitiesX((cellCount.x + 1) * cellCount.y),
      prevVelocitiesY(cellCount.x * (cellCount.y + 1)),
      tempPressure(cellCount.x * cellCount.y),
      targetSmoke(cellCount.x * cellCount.y) {

  std::mt19937 rng{std::random_device{}()};
  std::uniform_real_distribution<float> velocity(-1.f, 1.f);

  // for (float &v : velocitiesX) v = velocity(rng);
  // for (float &v : velocitiesY) v = velocity(rng);

  for (size_t x = 0; x < cellCount.x; x++) {
    solidCellMap[x] = true;                                   // Bottom
    solidCellMap[x + (cellCount.y - 1) * cellCount.x] = true; // Top
  }
  for (size_t y = 0; y < cellCount.y; y++) {
    solidCellMap[y * cellCount.x] = true;                   // Left
    solidCellMap[cellCount.x - 1 + y * cellCount.x] = true; // Right
  }

  // for (size_t x = 0; x < cellCount.x; x++) {
  // for (size_t y = 0; y < cellCount.y; y++) {
  // smoke(x, y) = 0.f;
  // }
  // }
}

float FluidGrid::sampleField(const std::vector<float> &field, float x, float y,
                             int strideY, int boundX, int boundY) {
  float sx = glm::clamp(x, 0.f, static_cast<float>(boundX) - 1.001f);
  float sy = glm::clamp(y, 0.f, static_cast<float>(boundY) - 1.001f);

  int x0 = static_cast<int>(sx);
  int y0 = static_cast<int>(sy);
  int x1 = x0 + 1;
  int y1 = y0 + 1;

  float tx = sx - x0;
  float ty = sy - y0;

  float v00 = field[x0 * strideY + y0];
  float v01 = field[x0 * strideY + y1];
  float v10 = field[x1 * strideY + y0];
  float v11 = field[x1 * strideY + y1];

  return glm::mix(glm::mix(v00, v01, ty), glm::mix(v10, v11, ty), tx);
}

float FluidGrid::calculateVelocityDivergence(glm::ivec2 cell) {
  float velocityTop = velY(cell.x, cell.y + 1);
  float velocityLeft = velX(cell.x, cell.y);
  float velocityRight = velX(cell.x + 1, cell.y);
  float velocityBottom = velY(cell.x, cell.y);

  return (velocityRight - velocityLeft + velocityTop - velocityBottom) /
         cellSize;
}

float FluidGrid::getPressure(glm::ivec2 cell) {
  if (cell.x < 0 || cell.x >= cellCount.x || cell.y < 0 ||
      cell.y >= cellCount.y)
    return 0.f;
  return pressureMap[cell.x + cell.y * cellCount.x];
}

float FluidGrid::pressureSolveCell(glm::ivec2 cell) {
  // Check neighbors for solids
  // Using 1.0f / 0.0f multipliers avoids branching
  float flowTop = isSolid({cell.x, cell.y + 1}) ? 0.f : 1.f;
  float flowLeft = isSolid({cell.x - 1, cell.y}) ? 0.f : 1.f;
  float flowRight = isSolid({cell.x + 1, cell.y}) ? 0.f : 1.f;
  float flowBottom = isSolid({cell.x, cell.y - 1}) ? 0.f : 1.f;

  float fluidEdgeCount = flowTop + flowLeft + flowRight + flowBottom;

  if (isSolid(cell) || fluidEdgeCount == 0.f)
    return 0.f;

  float pressureTop = getPressure({cell.x, cell.y + 1});
  float pressureLeft = getPressure({cell.x - 1, cell.y});
  float pressureRight = getPressure({cell.x + 1, cell.y});
  float pressureBottom = getPressure({cell.x, cell.y - 1});

  float velocityTop = velY(cell.x, cell.y + 1);
  float velocityLeft = velX(cell.x, cell.y);
  float velocityRight = velX(cell.x + 1, cell.y);
  float velocityBottom = velY(cell.x, cell.y);

  float pressureSum = (pressureTop * flowTop) + (pressureRight * flowRight) +
                      (pressureLeft * flowLeft) + (pressureBottom * flowBottom);

  float divergence =
      (velocityRight - velocityLeft + velocityTop - velocityBottom);

  return (pressureSum - density * cellSize * divergence / dt) / fluidEdgeCount;
}

void FluidGrid::solvePressure() {
  const int iterations = 40;

  for (int iter = 0; iter < iterations; ++iter) {
    // Parallelize this loop if using OpenMP: #pragma omp parallel for
    // collapse(2)
    for (int y = 0; y < cellCount.y; y++) {
      for (int x = 0; x < cellCount.x; x++) {
        // Optimized index access (x + y * width) matches getPressure
        tempPressure[x + y * cellCount.x] = pressureSolveCell({x, y});
      }
    }
    // Swap buffers
    std::swap(pressureMap, tempPressure);
  }
}

void FluidGrid::updateVelocities() {
  const float K = dt / (density * cellSize);

  // Update U (Horizontal)
  for (int x = 1; x < cellCount.x; x++) {
    for (int y = 0; y < cellCount.y; y++) {
      if (isSolid({x, y}) || isSolid({x - 1, y})) {
        velX(x, y) = 0.f;
      } else {
        float pRight = getPressure({x, y});
        float pLeft = getPressure({x - 1, y});
        velX(x, y) -= K * (pRight - pLeft);
      }
    }
  }

  // Update V (Vertical)
  for (int x = 0; x < cellCount.x; x++) {
    for (int y = 1; y < cellCount.y; y++) {
      if (isSolid({x, y}) || isSolid({x, y - 1})) {
        velY(x, y) = 0.f;
      } else {
        float pTop = getPressure({x, y});
        float pBottom = getPressure({x, y - 1});
        velY(x, y) -= K * (pTop - pBottom);
      }
    }
  }
}

// ----------------------------------------------------------------------------
// Advection
// ----------------------------------------------------------------------------

void FluidGrid::advectVelocities() {
  // Use pre-allocated member vectors instead of creating new ones
  std::copy(velocitiesX.begin(), velocitiesX.end(), prevVelocitiesX.begin());
  std::copy(velocitiesY.begin(), velocitiesY.end(), prevVelocitiesY.begin());

  // Advect U
  // Loop bounds: U grid is (W+1) x H
  for (int x = 0; x < cellCount.x + 1; x++) {
    for (int y = 0; y < cellCount.y; y++) {
      if (isSolid({x, y}) && isSolid({x - 1, y}))
        continue; // Optimization: don't advect deep inside walls

      // Position of U sample: Left face of cell (x, y) -> (x, y + 0.5)
      glm::vec2 pos(static_cast<float>(x), static_cast<float>(y) + 0.5f);
      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 prevPos = pos - vel * dt;

      // Sample U (Component 0 of velocity) at prevPos
      // To sample U specifically, we use the staggered offset logic manually or
      // via helper U lives at offset (0, 0.5) relative to grid top-left
      // sampleField expects grid coord, so we shift prevPos to U-grid-space:
      // U_grid_x = x, U_grid_y = y - 0.5
      prevVelocitiesX[x * cellCount.y + y] =
          sampleField(velocitiesX, prevPos.x, prevPos.y - 0.5f, cellCount.y,
                      cellCount.x + 1, cellCount.y);
    }
  }

  // Advect V
  // Loop bounds: V grid is W x (H+1)
  for (int x = 0; x < cellCount.x; x++) {
    for (int y = 0; y < cellCount.y + 1; y++) {
      if (isSolid({x, y}) && isSolid({x, y - 1}))
        continue;

      // Position of V sample: Bottom face of cell (x, y) -> (x + 0.5, y)
      glm::vec2 pos(static_cast<float>(x) + 0.5f, static_cast<float>(y));
      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 prevPos = pos - vel * dt;

      // Sample V (Component 1) at prevPos
      // V lives at offset (0.5, 0)
      // V_grid_x = x - 0.5, V_grid_y = y
      prevVelocitiesY[x * (cellCount.y + 1) + y] =
          sampleField(velocitiesY, prevPos.x - 0.5f, prevPos.y, cellCount.y + 1,
                      cellCount.x, cellCount.y + 1);
    }
  }

  // Swap pointers (move assignment)
  velocitiesX = prevVelocitiesX;
  velocitiesY = prevVelocitiesY;
}

void FluidGrid::advectSmoke() {
  // Use pre-allocated buffer
  // Note: We don't necessarily need to copy old values if we overwrite all
  // valid cells

  for (int x = 0; x < cellCount.x; x++) {
    for (int y = 0; y < cellCount.y; y++) {
      if (isSolid({x, y})) {
        targetSmoke[x * cellCount.y + y] = 0.0f;
        continue;
      }

      // Smoke is at Cell Center: (x + 0.5, y + 0.5)
      glm::vec2 pos{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 posPrev = pos - vel * dt;

      targetSmoke[x * cellCount.y + y] = getSmokeAtWorldPos(posPrev);
    }
  }
  smokeMap = targetSmoke;
}

// ----------------------------------------------------------------------------
// Sampling
// ----------------------------------------------------------------------------

glm::vec2 FluidGrid::getVelocityAtWorldPos(glm::vec2 worldPos) {
  // 1. Interpolate U
  // U is defined at (0, 0.5), (1, 0.5).
  // To sample at worldPos, we shift Y by -0.5 to align with U's grid lines.
  float u = sampleField(velocitiesX, worldPos.x, worldPos.y - 0.5f, cellCount.y,
                        cellCount.x + 1, cellCount.y);

  // 2. Interpolate V
  // V is defined at (0.5, 0), (0.5, 1).
  // To sample at worldPos, we shift X by -0.5 to align with V's grid lines.
  float v = sampleField(velocitiesY, worldPos.x - 0.5f, worldPos.y,
                        cellCount.y + 1, cellCount.x, cellCount.y + 1);

  return {u, v};
}

float FluidGrid::getSmokeAtWorldPos(glm::vec2 worldPos) {
  // Smoke is defined at cell centers (0.5, 0.5).
  // To map WorldPos to Smoke Array Index space:
  // (0.5, 0.5) -> (0, 0)
  // Therefore: index = world - 0.5
  return sampleField(smokeMap, worldPos.x - 0.5f, worldPos.y - 0.5f,
                     cellCount.y, cellCount.x, cellCount.y);
}

bool FluidGrid::isSolid(glm::ivec2 cell) {
  if (cell.x < 0 || cell.x >= cellCount.x || cell.y < 0 ||
      cell.y >= cellCount.y)
    return true; // Boundary is solid
  // Original index logic for solidMap (x + y * width ?)
  // The constructor filled it using: x + (cellCount.y - 1) * cellCount.x
  // That implies index = y * width + x?
  // Let's check constructor: solidCellMap[x + (cellCount.y - 1) * cellCount.x]
  // Yes, y * cellCount.x + x.
  return solidCellMap[cell.x + cell.y * cellCount.x];
}

} // namespace vkh
