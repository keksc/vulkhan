#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "../system.hpp"

#include <memory>
#include <vector>

namespace vkh {

class FluidGrid : public System {
public:
  FluidGrid(EngineContext &context, glm::ivec2 cellCount, float cellSize);
  ~FluidGrid();

  std::vector<float> velocitiesX;
  std::vector<float> velocitiesY;
  std::vector<float> targetVelocitiesX;
  std::vector<float> targetVelocitiesY;

  std::vector<float> pressureMap;
  std::vector<float> divergence;

  std::vector<float> smokeMap;
  std::vector<float> targetSmoke;

  std::vector<uint8_t> solidCellMap;

  float cellSize;
  glm::ivec2 cellCount;

  const float density = 1.f;
  const float dt = 1.f / 60.f;

  inline float &velX(int x, int y) {
    return velocitiesX[x + y * (cellCount.x + 1)];
  }
  inline float &velY(int x, int y) { return velocitiesY[x + y * cellCount.x]; }
  inline float &smoke(int x, int y) { return smokeMap[x + y * cellCount.x]; }

  inline float &prevVelX(int x, int y) {
    return targetVelocitiesX[x + y * (cellCount.x + 1)];
  }
  inline float &prevVelY(int x, int y) {
    return targetVelocitiesY[x + y * cellCount.x];
  }
  inline float &tSmoke(int x, int y) {
    return targetSmoke[x + y * cellCount.x];
  }
  bool isSolid(glm::uvec2 cell) {
    if (cell.x >= cellCount.x || cell.y >= cellCount.y)
      return true;
    return solidCellMap[cell.x + cell.y * cellCount.x];
  }

  void advectVelocities();
  void advectSmoke();

  void update();

  float calculateVelocityDivergence(glm::uvec2 cell);

  // Sampling / Interpolation
  glm::vec2 getVelocityAtWorldPos(glm::vec2 worldPos);
  float getSmokeAtWorldPos(glm::vec2 worldPos);

  vk::DescriptorSetLayout dyeImageSetLayout;
  vk::DescriptorSet dyeImageSet;

  struct ComputePushConstants {
    int rb;
    float cellSize;
    float dt;
  };

  vk::DescriptorSet computeSet;
  vk::DescriptorSetLayout computeSetLayout;
  vk::PipelineLayout computePipelineLayout;

  std::unique_ptr<ComputePipeline> divergencePipeline;
  std::unique_ptr<ComputePipeline> pressureSolvePipeline;
  std::unique_ptr<ComputePipeline> updateVelocitiesPipeline;

  std::unique_ptr<Image> dyeImage;
  std::unique_ptr<Image> velocityImage;
  std::unique_ptr<Image> targetVelocityImage;
  std::unique_ptr<Image> pressureImage;
  std::unique_ptr<Image> solidCellImage;
  std::unique_ptr<Image> divergenceImage;
  std::unique_ptr<Image> targetDyeImage;

private:
  float getPressure(glm::uvec2 cell);

  // Generic bilinear sampler
  float sampleField(const std::vector<float> &field, float x, float y,
                    int strideY, int boundX, int boundY);
};

} // namespace vkh
