#include "fluidGrid.hpp"
#include <cstring>
#include <vulkan/vulkan.hpp>

#include <glm/gtx/norm.hpp>

#include "../../descriptors.hpp"

#include <algorithm>

namespace vkh {

FluidGrid::FluidGrid(EngineContext &context, glm::uvec2 cellCount,
                     float cellSize)
    : System(context), cellCount{cellCount}, cellSize{cellSize},
      velocitiesX((cellCount.x + 1) * cellCount.y),
      velocitiesY(cellCount.x * (cellCount.y + 1)),
      pressureMap(cellCount.x * cellCount.y),
      smokeMap(cellCount.x * cellCount.y),
      solidCellMap(cellCount.x * cellCount.y),
      prevVelocitiesX((cellCount.x + 1) * cellCount.y),
      prevVelocitiesY(cellCount.x * (cellCount.y + 1)),
      tempPressure(cellCount.x * cellCount.y),
      targetSmoke(cellCount.x * cellCount.y) {

  ImageCreateInfo_empty createInfo{};
  createInfo.size = cellCount;
  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eStorage;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.name = "water displacement map";
  dyeImage = std::make_unique<Image>(context, createInfo);

  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment |
              vk::ShaderStageFlagBits::eCompute,
          nullptr},
  };
  updateSetLayout = buildDescriptorSetLayout(context, bindings);
  dyeImageSet =
      context.vulkan.globalDescriptorAllocator->allocate(updateSetLayout);

  DescriptorWriter writer(context);
  writer.writeImage(0,
                    dyeImage->getDescriptorInfo(context.vulkan.defaultSampler),
                    vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(dyeImageSet);

  for (size_t x = 0; x < cellCount.x; x++) {
    solidCellMap[x] = true;
    solidCellMap[x + (cellCount.y - 1) * cellCount.x] = true;
  }
  for (size_t y = 0; y < cellCount.y; y++) {
    solidCellMap[y * cellCount.x] = true;
    solidCellMap[cellCount.x - 1 + y * cellCount.x] = true;
  }

  for (size_t y = 100; y < 120; y++) {
    for (size_t x = 100; x < 120; x++) {
      if (glm::length2(glm::vec2{x - 110.f, y - 110.f}) < 100.f)
        solidCellMap[y * cellCount.x + x] = true;
    }
  }
}

FluidGrid::~FluidGrid() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyDescriptorSetLayout(updateSetLayout, nullptr);
  }
}

float FluidGrid::sampleField(const std::vector<float> &field, float x, float y,
                             int strideX, int boundX, int boundY) {
  glm::vec2 pos = glm::vec2(x, y);
  glm::vec2 minBound = glm::vec2(0.0f);
  glm::vec2 maxBound = glm::vec2(static_cast<float>(boundX) - 1.001f,
                                 static_cast<float>(boundY) - 1.001f);

  pos = glm::clamp(pos, minBound, maxBound);

  glm::ivec2 iPos = glm::floor(pos);
  glm::vec2 t = pos - glm::vec2(iPos);

  int idx00 = iPos.x + iPos.y * strideX;
  int idx01 = idx00 + strideX;

  float mix0 = glm::mix(field[idx00], field[idx00 + 1], t.x);
  float mix1 = glm::mix(field[idx01], field[idx01 + 1], t.x);

  return glm::mix(mix0, mix1, t.y);
}

float FluidGrid::calculateVelocityDivergence(glm::uvec2 cell) {
  float div = velX(cell.x + 1, cell.y) - velX(cell.x, cell.y) +
              velY(cell.x, cell.y + 1) - velY(cell.x, cell.y);
  return div;
}

float FluidGrid::getPressure(glm::uvec2 cell) {
  if (cell.x < 0 || cell.x >= cellCount.x || cell.y < 0 ||
      cell.y >= cellCount.y)
    return 0.f;
  return pressureMap[cell.x + cell.y * cellCount.x];
}

void FluidGrid::solvePressure() {
  const int w = cellCount.x;
  const int h = cellCount.y;
  const float scale = 0.25f;
  const float dt_rho_size = (density * cellSize / dt);

#pragma omp parallel for collapse(2)
  for (int y = 1; y < h - 1; y++) {
    for (int x = 1; x < w - 1; x++) {
      if (solidCellMap[x + y * w]) {
        tempPressure[x + y * w] = 0.0f;
      } else {
        float div = velX(x + 1, y) - velX(x, y) + velY(x, y + 1) - velY(x, y);
        tempPressure[x + y * w] = div * dt_rho_size;
      }
    }
  }

  for (int iter = 0; iter < 40; iter++) {
    for (int rb = 0; rb < 2; rb++) {
#pragma omp parallel for
      for (int y = 1; y < h - 1; y++) {
        int xStart = 1 + ((y + rb) % 2);
        for (int x = xStart; x < w - 1; x += 2) {
          int idx = x + y * w;
          if (solidCellMap[idx])
            continue;

          pressureMap[idx] = (pressureMap[idx - 1] + pressureMap[idx + 1] +
                              pressureMap[idx - w] + pressureMap[idx + w] -
                              tempPressure[idx]) *
                             scale;
        }
      }
    }
  }
}

void FluidGrid::advectVelocities() {
#pragma omp parallel for collapse(2)
  for (int y = 0; y < cellCount.y; y++) {
    for (int x = 0; x < cellCount.x + 1; x++) {
      glm::vec2 pos{(float)x, (float)y + 0.5f};
      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 prevPos = pos - vel * dt;
      prevVelX(x, y) =
          sampleField(velocitiesX, prevPos.x, prevPos.y - 0.5f, cellCount.x + 1,
                      cellCount.x + 1, cellCount.y);
    }
  }

#pragma omp parallel for collapse(2)
  for (int y = 0; y < cellCount.y + 1; y++) {
    for (int x = 0; x < cellCount.x; x++) {
      glm::vec2 pos{(float)x + 0.5f, (float)y};
      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 prevPos = pos - vel * dt;
      prevVelY(x, y) = sampleField(velocitiesY, prevPos.x - 0.5f, prevPos.y,
                                   cellCount.x, cellCount.x, cellCount.y + 1);
    }
  }

  std::swap(velocitiesX, prevVelocitiesX);
  std::swap(velocitiesY, prevVelocitiesY);
}

void FluidGrid::updateVelocities() {
  const float K = dt / (density * cellSize);

  for (int y = 0; y < cellCount.y; y++) {
    for (int x = 1; x < cellCount.x; x++) {
      if (isSolid({x, y}) || isSolid({x - 1, y})) {
        velX(x, y) = 0.f;
      } else {
        float pRight = getPressure({x, y});
        float pLeft = getPressure({x - 1, y});
        velX(x, y) -= K * (pRight - pLeft);
      }
    }
  }

  for (int y = 1; y < cellCount.y; y++) {
    for (int x = 0; x < cellCount.x; x++) {
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

void FluidGrid::advectSmoke() {
#pragma omp parallel for collapse(2)
  for (int y = 0; y < cellCount.y; y++) {
    for (int x = 0; x < cellCount.x; x++) {
      if (isSolid({x, y})) {
        targetSmoke[x + y * cellCount.x] = 0.0f;
        continue;
      }

      glm::vec2 pos{static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f};
      glm::vec2 vel = getVelocityAtWorldPos(pos);
      glm::vec2 prevPos = pos - vel * (dt / cellSize);

      targetSmoke[x + y * cellCount.x] = getSmokeAtWorldPos(prevPos);
    }
  }
  smokeMap.swap(targetSmoke);
}

void FluidGrid::update() {
  solvePressure();
  updateVelocities();
  advectVelocities();
  advectSmoke();
}

glm::vec2 FluidGrid::getVelocityAtWorldPos(glm::vec2 worldPos) {
  float u = sampleField(velocitiesX, worldPos.x, worldPos.y - 0.5f,
                        cellCount.x + 1, cellCount.x + 1, cellCount.y);

  float v = sampleField(velocitiesY, worldPos.x - 0.5f, worldPos.y, cellCount.x,
                        cellCount.x, cellCount.y + 1);

  return {u, v};
}

float FluidGrid::getSmokeAtWorldPos(glm::vec2 worldPos) {
  return sampleField(smokeMap, worldPos.x - 0.5f, worldPos.y - 0.5f,
                     cellCount.x, cellCount.x, cellCount.y);
}

} // namespace vkh
