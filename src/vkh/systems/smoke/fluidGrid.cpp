#include "fluidGrid.hpp"
#include <cstring>
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <cmath>

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"

namespace vkh {

FluidGrid::FluidGrid(EngineContext &context, glm::ivec3 cellCount,
                     float cellSize)
    : System(context), cellCount{cellCount}, cellSize{cellSize},
      velocitiesX(cellCount.x * cellCount.y * cellCount.z),
      velocitiesY(cellCount.x * cellCount.y * cellCount.z),
      velocitiesZ(cellCount.x * cellCount.y * cellCount.z),
      targetVelocitiesX(cellCount.x * cellCount.y * cellCount.z),
      targetVelocitiesY(cellCount.x * cellCount.y * cellCount.z),
      targetVelocitiesZ(cellCount.x * cellCount.y * cellCount.z),
      pressureMap(cellCount.x * cellCount.y * cellCount.z),
      divergence(cellCount.x * cellCount.y * cellCount.z),
      smokeMap(cellCount.x * cellCount.y * cellCount.z),
      targetSmoke(cellCount.x * cellCount.y * cellCount.z),
      solidCellMap(cellCount.x * cellCount.y * cellCount.z) {

  ImageCreateInfo_empty3D createInfo{};
  createInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                     vk::ImageUsageFlagBits::eTransferSrc |
                     vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eStorage;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.size = cellCount;

  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke dye volume";
  dyeImage = std::make_unique<Image3D>(context, createInfo);

  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke divergence volume";
  divergenceImage = std::make_unique<Image3D>(context, createInfo);

  // rgba32f, not r32g32b32Sfloat: storage images generally can't be
  // 3-component, and the compute shaders (common.glsl) address velocity as
  // one combined rgba32f volume (xyz = velX/velY/velZ, w unused) rather
  // than three separate staggered images - see the header's note on why
  // velocity is co-located instead of a true staggered MAC grid here.
  createInfo.format = vk::Format::eR32G32B32A32Sfloat;
  createInfo.name = "smoke velocity volume";
  velocityImage = std::make_unique<Image3D>(context, createInfo);

  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke pressure volume";
  pressureImage = std::make_unique<Image3D>(context, createInfo);

  createInfo.format =
      vk::Format::eR8Uint; // actually bools; TODO: pack 8 per byte
  createInfo.name = "smoke solid cell volume";
  solidCellImage = std::make_unique<Image3D>(context, createInfo);

  createInfo.format = vk::Format::eR32G32B32A32Sfloat;
  createInfo.name = "smoke target velocity volume";
  targetVelocityImage = std::make_unique<Image3D>(context, createInfo);

  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke target dye volume";
  targetDyeImage = std::make_unique<Image3D>(context, createInfo);

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

  // NOTE: this needs a REPEAT-mode sampler, not context.vulkan.defaultSampler
  // (presumably clamp-to-edge) - the volume is periodic, so sampling near a
  // wrapped edge with a clamping sampler will smear instead of continuing
  // across. Swap in a dedicated repeat sampler here once one exists.
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
      {4, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {5, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {6, vk::DescriptorType::eStorageImage, 1,
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
  computeWriter.writeImage(
      4, dyeImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.writeImage(
      5, targetVelocityImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.writeImage(
      6, targetDyeImage->getDescriptorInfo(context.vulkan.defaultSampler),
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
  advectPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/smoke/advect.comp.spv", computePipelineLayout,
      "smoke advect compute pipeline");

  vk::PushConstantRange brushPcRange{};
  brushPcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  brushPcRange.offset = 0;
  brushPcRange.size = sizeof(BrushPushConstants);

  vk::PipelineLayoutCreateInfo brushLayoutInfo{};
  brushLayoutInfo.setLayoutCount = 1;
  brushLayoutInfo.pSetLayouts = &computeSetLayout;
  brushLayoutInfo.pushConstantRangeCount = 1;
  brushLayoutInfo.pPushConstantRanges = &brushPcRange;

  if (context.vulkan.device.createPipelineLayout(&brushLayoutInfo, nullptr,
                                                 &brushPipelineLayout) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create brush pipeline layout!");

  brushPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/smoke/brush.comp.spv", brushPipelineLayout,
      "smoke brush compute pipeline");

  // gridOrigin starts at {0,0,0}; populate every cell in that initial
  // window from solidQueryFn (default: open air) via the same path
  // recenter() uses later, so there's exactly one code path for "give this
  // cell fresh data".
  reinitAll();

  // reinitAll() above only touches the CPU-side solidCellMap (which then
  // gets uploaded on the first update() via solidMapDirty). The float
  // images have no CPU-side mirror anymore, so they need an explicit clear
  // or frame 1 starts from undefined device memory.
  clearImages();
}

void FluidGrid::clearImages() {
  vk::CommandBuffer cmd = beginSingleTimeCommands(context);
  debug::beginLabel(context, cmd, "FluidGrid::clearImages",
                    {0.4f, 0.4f, 0.4f, 1.0f});

  vk::ClearColorValue clearValue{};
  clearValue.setFloat32({0.0f, 0.0f, 0.0f, 0.0f});

  vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

  for (Image3D *img : {dyeImage.get(), velocityImage.get(), pressureImage.get(),
                       divergenceImage.get(), targetVelocityImage.get(),
                       targetDyeImage.get()}) {
    cmd.clearColorImage(*img, vk::ImageLayout::eGeneral, clearValue, range);
  }

  debug::endLabel(context, cmd);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

FluidGrid::~FluidGrid() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyPipelineLayout(computePipelineLayout, nullptr);
    context.vulkan.device.destroyPipelineLayout(brushPipelineLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(computeSetLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(dyeImageSetLayout,
                                                     nullptr);
  }
}

void FluidGrid::reinitWorldCell(glm::ivec3 worldCell) {
  int idx = localIndex(worldCell);
  solidCellMap[idx] = solidQueryFn(worldCell) ? 1 : 0;
  velocitiesX[idx] = 0.0f;
  velocitiesY[idx] = 0.0f;
  velocitiesZ[idx] = 0.0f;
  smokeMap[idx] = 0.0f;
  pressureMap[idx] = 0.0f;
}

void FluidGrid::reinitSlice(int axis, int worldAxisCoord) {
  int axisB = (axis + 1) % 3;
  int axisC = (axis + 2) % 3;

  glm::ivec3 w{0};
  w[axis] = worldAxisCoord;
  for (int b = 0; b < cellCount[axisB]; b++) {
    w[axisB] = gridOrigin[axisB] + b;
    for (int c = 0; c < cellCount[axisC]; c++) {
      w[axisC] = gridOrigin[axisC] + c;
      reinitWorldCell(w);
    }
  }
  solidMapDirty = true;
}

void FluidGrid::reinitAll() {
  for (int x = 0; x < cellCount.x; x++)
    for (int y = 0; y < cellCount.y; y++)
      for (int z = 0; z < cellCount.z; z++)
        reinitWorldCell(gridOrigin + glm::ivec3(x, y, z));
  solidMapDirty = true;
}

void FluidGrid::recenter(glm::vec3 playerWorldPos) {
  glm::ivec3 playerCell = glm::ivec3(glm::floor(playerWorldPos / cellSize));

  for (int axis = 0; axis < 3; axis++) {
    int n = cellCount[axis];
    int windowCenter = gridOrigin[axis] + n / 2;
    int drift = playerCell[axis] - windowCenter;
    if (std::abs(drift) < recenterThreshold)
      continue;

    int oldOrigin = gridOrigin[axis];
    int newOrigin = playerCell[axis] - n / 2;
    int shift = newOrigin - oldOrigin;
    gridOrigin[axis] = newOrigin;

    if (std::abs(shift) >= n) {
      // Moved further in one jump than the grid spans - nothing overlaps
      // the old window, so just refresh everything instead of sweeping
      // slice-by-slice.
      reinitAll();
      continue;
    }

    int exposedCount = std::abs(shift);
    for (int i = 0; i < exposedCount; i++) {
      int worldAxisCoord = shift > 0 ? oldOrigin + n + i : newOrigin + i;
      reinitSlice(axis, worldAxisCoord);
    }
  }
}

void FluidGrid::update(glm::ivec3 brushCenterCell, bool brushActive,
                       float brushRadiusCells, glm::vec3 brushVelocityDelta,
                       float brushSmokeRate, glm::ivec3 playerCenterCell,
                       bool playerPushActive, float playerRadiusCells,
                       glm::vec3 playerVelocityDelta, bool laserActive,
                       glm::vec3 laserOriginWorld, glm::vec3 laserDirection,
                       float laserRadiusCells,
                       glm::vec3 laserVelocityDelta) {
  vk::CommandBuffer cmd = beginSingleTimeCommands(context);
  debug::beginLabel(context, cmd, "FluidGrid::update", {0.3f, 0.6f, 0.3f, 1.0f});

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout,
                         0, computeSet, {});

  const uint32_t groupsX = (cellCount.x + 7) / 8;
  const uint32_t groupsY = (cellCount.y + 7) / 8;
  const uint32_t groupsZ = (cellCount.z + 7) / 8;

  vk::MemoryBarrier storageImageBarrier{vk::AccessFlagBits::eShaderWrite,
                                        vk::AccessFlagBits::eShaderRead |
                                            vk::AccessFlagBits::eShaderWrite};
  auto barrier = [&] {
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, {},
                        storageImageBarrier, {}, {});
  };

  // Same as above, but for the compute->transfer handoff before copyImage:
  // copyImage runs on the eTransfer stage, so a barrier whose dstStageMask
  // is eComputeShader (as in barrier() above) gives the GPU no ordering
  // guarantee against it - the transfer engine can start reading
  // targetVelocityImage/targetDyeImage before advect's shader invocations
  // have actually finished writing them. That race is what was making the
  // brush's contribution disappear: it usually lost the race against the
  // (now-zeroed) target images.
  vk::MemoryBarrier computeToTransferBarrier{
      vk::AccessFlagBits::eShaderWrite, vk::AccessFlagBits::eTransferRead};
  auto transferBarrier = [&] {
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer, {},
                        computeToTransferBarrier, {}, {});
  };

  // --- Only re-upload solid cells when recenter() actually changed them ---
  if (solidMapDirty) {
    debug::beginLabel(context, cmd, "smoke: upload solid cells",
                      {0.6f, 0.6f, 0.6f, 1.0f});
    Buffer<uint8_t> solidStagingBuffer(
        context, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        solidCellMap.size());
    solidStagingBuffer.map();
    std::memcpy(solidStagingBuffer.getMappedAddr(), solidCellMap.data(),
                solidCellMap.size() * sizeof(uint8_t));
    solidStagingBuffer.unmap();
    solidCellImage->copyFromBuffer(
        solidStagingBuffer); // still a small upload; consider moving this onto
                             // `cmd` too instead of its own transfer if
                             // copyFromBuffer submits separately
    solidMapDirty = false;
    debug::endLabel(context, cmd);
  }

  if (brushActive) {
    debug::beginLabel(context, cmd, "smoke: brush", {0.9f, 0.5f, 0.1f, 1.0f});
    applyBrush(cmd, brushCenterCell, brushRadiusCells, brushVelocityDelta,
               brushSmokeRate);
    // Re-bind descriptor set back to computePipelineLayout for divergence pass
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computePipelineLayout, 0, computeSet, {});
    barrier();
    debug::endLabel(context, cmd);
  }

  if (playerPushActive && playerRadiusCells > 0.0f) {
    debug::beginLabel(context, cmd, "smoke: player push",
                      {0.1f, 0.7f, 0.9f, 1.0f});
    // smokeRate = 0: this only perturbs velocity, never adds dye - the
    // player pushing air around shouldn't itself look like a smoke source.
    applyBrush(cmd, playerCenterCell, playerRadiusCells, playerVelocityDelta,
              0.0f);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computePipelineLayout, 0, computeSet, {});
    barrier();
    debug::endLabel(context, cmd);
  }

  if (laserActive && laserRadiusCells > 0.0f &&
      glm::length(laserDirection) > 0.0f) {
    debug::beginLabel(context, cmd, "smoke: laser", {0.9f, 0.1f, 0.9f, 1.0f});
    applyLaser(cmd, laserOriginWorld, laserDirection, laserRadiusCells,
              laserVelocityDelta);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computePipelineLayout, 0, computeSet, {});
    barrier();
    debug::endLabel(context, cmd);
  }

  ComputePushConstants pc{};
  pc.cellSize = cellSize;
  pc.dt = dt;
  pc.cellCount = glm::ivec4(cellCount, 0);

  debug::beginLabel(context, cmd, "smoke: divergence", {0.2f, 0.5f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *divergencePipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrier();
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: pressure solve",
                    {0.2f, 0.7f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pressureSolvePipeline);
  for (int iter = 0; iter < 40; iter++) {
    for (int rb = 0; rb < 2; rb++) {
      pc.rb = rb;
      cmd.pushConstants(computePipelineLayout,
                        vk::ShaderStageFlagBits::eCompute, 0,
                        sizeof(ComputePushConstants), &pc);
      cmd.dispatch(groupsX, groupsY, groupsZ);
      barrier();
    }
  }
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: update velocities",
                    {0.2f, 0.9f, 0.5f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *updateVelocitiesPipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrier();
  debug::endLabel(context, cmd);

  // --- Advection: velocities/dye -> targetVelocities/targetDye ---
  debug::beginLabel(context, cmd, "smoke: advect", {0.7f, 0.2f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *advectPipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  transferBarrier();
  debug::endLabel(context, cmd);

  // --- Copy targets back into the "current" images, GPU-side, no CPU visit ---
  debug::beginLabel(context, cmd, "smoke: copy targets back",
                    {0.9f, 0.9f, 0.2f, 1.0f});
  vk::ImageCopy region{};
  region.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
  region.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
  region.extent = vk::Extent3D{static_cast<uint32_t>(cellCount.x),
                               static_cast<uint32_t>(cellCount.y),
                               static_cast<uint32_t>(cellCount.z)};

  cmd.copyImage(*targetVelocityImage, vk::ImageLayout::eGeneral, *velocityImage,
                vk::ImageLayout::eGeneral, region);
  cmd.copyImage(*targetDyeImage, vk::ImageLayout::eGeneral, *dyeImage,
                vk::ImageLayout::eGeneral, region);
  debug::endLabel(context, cmd);

  debug::endLabel(context, cmd); // matches "FluidGrid::update"
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void FluidGrid::applyBrush(vk::CommandBuffer cmd, glm::ivec3 centerCell,
                           float radiusCells, glm::vec3 velocityDelta,
                           float smokeRate) {
  int radiusInt = static_cast<int>(std::ceil(radiusCells));
  int boxSize = radiusInt * 2 + 1;

  BrushPushConstants bpc{};
  bpc.cellCount = glm::ivec4(cellCount, 0);
  bpc.brushMinCell = glm::ivec4(centerCell - glm::ivec3(radiusInt), boxSize);
  bpc.brushCenterAndRadius = glm::vec4(glm::vec3(centerCell), radiusCells);
  bpc.velocityDeltaAndSmoke = glm::vec4(velocityDelta, smokeRate);

  // Different pipeline layout (different push-constant range) than the rest
  // of update()'s dispatches, so the descriptor set bound against
  // computePipelineLayout is no longer valid here - rebind it against
  // brushPipelineLayout before this dispatch.
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, brushPipelineLayout,
                         0, computeSet, {});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *brushPipeline);
  cmd.pushConstants(brushPipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(BrushPushConstants), &bpc);

  uint32_t groups = (boxSize + 7) / 8;
  cmd.dispatch(groups, groups, groups);
}

void FluidGrid::applyLaser(vk::CommandBuffer cmd, glm::vec3 originWorld,
                           glm::vec3 direction, float radiusCells,
                           glm::vec3 velocityDelta) {
  glm::vec3 dir = glm::normalize(direction);

  // Space samples ~1 radius apart so consecutive spheres overlap slightly
  // and leave no gaps along the beam.
  float stepCells = std::max(radiusCells, 1.0f);

  // The grid is toroidal, so a straight ray re-enters cells it already
  // touched once it's traveled past the local window's extent on every
  // axis - going further just repaints the same physical cells. The
  // longest useful distance is therefore the window's cell-space diagonal;
  // round up so a beam fired straight down an axis still reaches the far
  // face.
  float travelCells = glm::length(glm::vec3(cellCount));
  int numSteps = static_cast<int>(std::ceil(travelCells / stepCells));

  // Same barrier every other dispatch in update() uses: brush is a
  // read-modify-write on the velocity/dye storage images, and consecutive
  // spheres along the beam can overlap, so each step's write must be
  // visible before the next step reads the same texels.
  vk::MemoryBarrier storageImageBarrier{vk::AccessFlagBits::eShaderWrite,
                                        vk::AccessFlagBits::eShaderRead |
                                            vk::AccessFlagBits::eShaderWrite};

  for (int i = 0; i <= numSteps; i++) {
    glm::vec3 sampleWorld = originWorld + dir * (stepCells * cellSize) *
                                              static_cast<float>(i);
    glm::ivec3 centerCell =
        glm::ivec3(glm::floor(sampleWorld / cellSize));

    applyBrush(cmd, centerCell, radiusCells, velocityDelta, 0.0f);

    if (i != numSteps) {
      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                          vk::PipelineStageFlagBits::eComputeShader, {},
                          storageImageBarrier, {}, {});
    }
  }
}

} // namespace vkh
