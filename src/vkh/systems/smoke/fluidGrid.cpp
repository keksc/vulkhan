#include "fluidGrid.hpp"
#include <cstring>
#include <vulkan/vulkan.hpp>

#include <glm/gtx/norm.hpp>

#include "../../buffer.hpp"
#include "../../descriptors.hpp"

#include <algorithm>

namespace vkh {

FluidGrid::FluidGrid(EngineContext &context, glm::ivec2 cellCount,
                     float cellSize)
    : System(context), cellCount{cellCount}, cellSize{cellSize},
      velocitiesX((cellCount.x + 1) * cellCount.y),
      velocitiesY(cellCount.x * (cellCount.y + 1)),
      pressureMap(cellCount.x * cellCount.y),
      smokeMap(cellCount.x * cellCount.y),
      solidCellMap(cellCount.x * cellCount.y),
      targetVelocitiesX((cellCount.x + 1) * cellCount.y),
      targetVelocitiesY(cellCount.x * (cellCount.y + 1)),
      divergence(cellCount.x * cellCount.y),
      targetSmoke(cellCount.x * cellCount.y) {

  ImageCreateInfo_empty createInfo{};
  createInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc |
                     vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eStorage;

  createInfo.size = cellCount;
  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.name = "smoke dye image";
  dyeImage = std::make_unique<Image>(context, createInfo);

  createInfo.size = {cellCount.x, cellCount.y};
  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke divergence image";
  divergenceImage = std::make_unique<Image>(context, createInfo);

  createInfo.size = {cellCount.x + 1, cellCount.y + 1};
  createInfo.format = vk::Format::eR32G32Sfloat;
  createInfo.name = "smoke velocity image";
  velocityImage = std::make_unique<Image>(context, createInfo);

  createInfo.size = cellCount;
  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke pressure image";
  pressureImage = std::make_unique<Image>(context, createInfo);

  createInfo.format =
      vk::Format::eR8Uint; // actually bools. TODO: pack 8 into 1 to save memory
  createInfo.name = "smoke solid cell image";
  solidCellImage = std::make_unique<Image>(context, createInfo);

  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment |
              vk::ShaderStageFlagBits::eCompute,
          nullptr},
  };
  dyeImageSetLayout = buildDescriptorSetLayout(context, bindings);
  dyeImageSet =
      context.vulkan.globalDescriptorAllocator->allocate(dyeImageSetLayout);

  DescriptorWriter writer(context);
  writer.writeImage(0,
                    dyeImage->getDescriptorInfo(context.vulkan.defaultSampler),
                    vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(dyeImageSet);

  bindings = {
      {0, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {1, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {2, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {3, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
  };
  computeSetLayout = buildDescriptorSetLayout(context, bindings);
  computeSet =
      context.vulkan.globalDescriptorAllocator->allocate(computeSetLayout);

  DescriptorWriter computeWriter(context);
  computeWriter.writeImage(
      0, divergenceImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.writeImage(
      1, velocityImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.writeImage(
      2, pressureImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.writeImage(
      3, solidCellImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.updateSet(computeSet);

  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  pcRange.offset = 0;
  pcRange.size = sizeof(ComputePushConstants);

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &computeSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pcRange;

  if (context.vulkan.device.createPipelineLayout(&pipelineLayoutInfo, nullptr,
                                                 &computePipelineLayout) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create pipeline layout!");

  divergencePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/smoke/divergence.comp.spv", computePipelineLayout,
      "smoke divergence compute pipeline");
  pressureSolvePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/smoke/pressureSolve.comp.spv", computePipelineLayout,
      "smoke pressure solver compute pipeline");
  updateVelocitiesPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/smoke/updateVelocities.comp.spv", computePipelineLayout,
      "smoke update velocities compute pipeline");

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
    context.vulkan.device.destroyPipelineLayout(computePipelineLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(computeSetLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(dyeImageSetLayout,
                                                     nullptr);
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

  std::swap(velocitiesX, targetVelocitiesX);
  std::swap(velocitiesY, targetVelocitiesY);
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
  glm::uvec2 uploadSize = cellCount + glm::ivec2(1, 1); // 481 x 481
  std::vector<glm::vec2> packedVelocities(uploadSize.x * uploadSize.y,
                                          glm::vec2(0.f));

  for (uint32_t y = 0; y < uploadSize.y; y++) {
    for (uint32_t x = 0; x < uploadSize.x; x++) {
      float u = 0.f;
      float v = 0.f;

      // velocitiesX size is (cellCount.x + 1) * cellCount.y
      if (x <= cellCount.x && y < cellCount.y) {
        u = velX(x, y);
      }
      // velocitiesY size is cellCount.x * (cellCount.y + 1)
      if (x < cellCount.x && y <= cellCount.y) {
        v = velY(x, y);
      }

      packedVelocities[x + y * uploadSize.x] = glm::vec2(u, v);
    }
  }

  // Create temporary local staging buffer matching the 481x481 requirements
  Buffer<glm::vec2> velStagingBuffer(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      packedVelocities.size() // Sized to 231,361 elements (1,850,888 bytes)
  );

  velStagingBuffer.map();
  std::memcpy(velStagingBuffer.getMappedAddr(), packedVelocities.data(),
              packedVelocities.size() * sizeof(glm::vec2));
  velStagingBuffer.unmap();

  velocityImage->copyFromBuffer(velStagingBuffer);

  // --- B. Upload Solid Cells ---
  Buffer<uint8_t> solidStagingBuffer(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      solidCellMap.size());

  solidStagingBuffer.map();
  std::memcpy(solidStagingBuffer.getMappedAddr(), solidCellMap.data(),
              solidCellMap.size() * sizeof(uint8_t));
  solidStagingBuffer.unmap();

  solidCellImage->copyFromBuffer(solidStagingBuffer);

  // =================================================================
  // 2. SIMULATION STAGE: divergence + 40 iterations of red-black
  //    Gauss-Seidel pressure solve, all on the GPU.
  // =================================================================

  // Confirmed from buffer.hpp: these are free functions from
  // deviceHelpers.hpp (pulled in transitively via "../../buffer.hpp"),
  // the same ones Buffer<T>::copyFromBuffer uses internally.
  vk::CommandBuffer cmd = beginSingleTimeCommands(context);

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout,
                         0, computeSet, {});

  const uint32_t groupsX = (cellCount.x + 15) / 16;
  const uint32_t groupsY = (cellCount.y + 15) / 16;

  vk::MemoryBarrier storageImageBarrier{vk::AccessFlagBits::eShaderWrite,
                                        vk::AccessFlagBits::eShaderRead |
                                            vk::AccessFlagBits::eShaderWrite};
  auto barrier = [&] {
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, {},
                        storageImageBarrier, {}, {});
  };

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *divergencePipeline);

  ComputePushConstants divPc{};
  divPc.cellSize = cellSize;
  divPc.dt = dt;
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &divPc);
  cmd.dispatch(groupsX, groupsY, 1);

  barrier();

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pressureSolvePipeline);

  // Note: we deliberately do NOT clear pressureImage to 0 here.
  // pressureImage persists across frames on the GPU, so the solver starts
  // from last frame's converged result - matching the CPU version, which
  // also never resets pressureMap before its Gauss-Seidel loop. Clearing it
  // per-frame would still be correct, just slower to converge.
  for (int iter = 0; iter < 40; iter++) {
    for (int rb = 0; rb < 2; rb++) {
      ComputePushConstants rbPc{};
      rbPc.rb = rb;
      cmd.pushConstants(computePipelineLayout,
                        vk::ShaderStageFlagBits::eCompute, 0,
                        sizeof(ComputePushConstants), &rbPc);
      cmd.dispatch(groupsX, groupsY, 1);

      // Required after every single dispatch: the next color's read of a
      // neighbour pixel depends on this dispatch's write to that pixel.
      barrier();
    }
  }

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *updateVelocitiesPipeline);

  cmd.dispatch(groupsX, groupsY, 1);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  pressureImage->downloadPixels(
      reinterpret_cast<unsigned char *>(pressureMap.data()), 0);

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
