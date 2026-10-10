#include "smoke.hpp"

#include <GLFW/glfw3.h>
#include <cstring>
#include <vulkan/vulkan.hpp>

#include <algorithm>
#include <cmath>

#include "../../buffer.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"

namespace vkh {

// 64^3 = ~262k cells - a starting point for real-time GPU pressure solves
// (40 red-black Gauss-Seidel iterations/frame) plus O(n) CPU advection.
// Bump this once you've profiled headroom; it's independent of world scale
// since cellSize controls how much world space each cell covers.
SmokeSys::SmokeSys(EngineContext &context)
    : System(context), cellCount{64, 64, 64}, cellSize{1.f},
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

  createInfo.format = vk::Format::eR32G32B32A32Sfloat;
  createInfo.name = "smoke scratch velocity volume (MacCormack phi_hat_n)";
  scratchVelocityImage = std::make_unique<Image3D>(context, createInfo);

  createInfo.format = vk::Format::eR32Sfloat;
  createInfo.name = "smoke scratch dye volume (MacCormack phi_hat_n)";
  scratchDyeImage = std::make_unique<Image3D>(context, createInfo);

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

  // Linear filtering + repeat addressing, unlike context.vulkan.defaultSampler
  // (presumably nearest and/or clamp-to-edge). Repeat matters because the
  // volume is periodic - clamping at a wrapped face smears instead of
  // continuing across. Linear matters because without it every voxel reads
  // as a hard-edged cube up close instead of a smoothly interpolated field;
  // this is created explicitly rather than assumed because R32_SFLOAT (the
  // dye image's format) isn't guaranteed to support linear filtering on
  // every implementation the way normalized/8-bit formats are - if your GPU
  // doesn't support it, you'll silently get nearest-neighbor sampling
  // again despite eLinear being requested here, and dropping dyeImage's
  // format to something like R16Sfloat (with a matching change to the r32f
  // qualifier in common.glsl) would be the next thing to try.
  vk::SamplerCreateInfo dyeSamplerInfo{};
  dyeSamplerInfo.magFilter = vk::Filter::eLinear;
  dyeSamplerInfo.minFilter = vk::Filter::eLinear;
  dyeSamplerInfo.mipmapMode = vk::SamplerMipmapMode::eNearest;
  dyeSamplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  dyeSamplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  dyeSamplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  dyeSamplerInfo.minLod = 0.0f;
  dyeSamplerInfo.maxLod = 0.0f;
  dyeSamplerInfo.anisotropyEnable = VK_FALSE;
  dyeSamplerInfo.unnormalizedCoordinates = VK_FALSE;

  if (context.vulkan.device.createSampler(&dyeSamplerInfo, nullptr,
                                          &dyeSampler) != vk::Result::eSuccess)
    throw std::runtime_error("failed to create dye sampler!");

  DescriptorWriter writer(context);
  writer.writeImage(0, dyeImage->getDescriptorInfo(dyeSampler),
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
      {7, vk::DescriptorType::eStorageImage, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      {8, vk::DescriptorType::eStorageImage, 1,
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
  computeWriter.writeImage(
      7, scratchVelocityImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.writeImage(
      8, scratchDyeImage->getDescriptorInfo(context.vulkan.defaultSampler),
      vk::DescriptorType::eStorageImage);
  computeWriter.updateSet(computeSet);

  computePipelineLayout =
      createComputePipelineLayout(sizeof(ComputePushConstants), "sim step");

  divergencePipeline =
      makeComputePipeline("shaders/smoke/divergence.comp.spv",
                          computePipelineLayout,
                          "smoke divergence compute pipeline");
  pressureSolvePipeline =
      makeComputePipeline("shaders/smoke/pressureSolve.comp.spv",
                          computePipelineLayout,
                          "smoke pressure solver compute pipeline");
  updateVelocitiesPipeline =
      makeComputePipeline("shaders/smoke/updateVelocities.comp.spv",
                          computePipelineLayout,
                          "smoke update velocities compute pipeline");
  vorticityPipeline =
      makeComputePipeline("shaders/smoke/vorticityConfinement.comp.spv",
                          computePipelineLayout,
                          "smoke vorticity confinement compute pipeline");
  advectPipeline =
      makeComputePipeline("shaders/smoke/advect.comp.spv",
                          computePipelineLayout,
                          "smoke advect (MacCormack forward) compute pipeline");
  advectBackwardPipeline = makeComputePipeline(
      "shaders/smoke/advectBackward.comp.spv", computePipelineLayout,
      "smoke advect (MacCormack backward) compute pipeline");
  macCormackCombinePipeline = makeComputePipeline(
      "shaders/smoke/macCormackCombine.comp.spv", computePipelineLayout,
      "smoke advect (MacCormack combine) compute pipeline");

  brushPipelineLayout =
      createComputePipelineLayout(sizeof(BrushPushConstants), "brush");
  brushPipeline =
      makeComputePipeline("shaders/smoke/brush.comp.spv", brushPipelineLayout,
                          "smoke brush compute pipeline");

  reinitPipelineLayout =
      createComputePipelineLayout(sizeof(ReinitPushConstants), "reinit");
  reinitRegionPipeline =
      makeComputePipeline("shaders/smoke/reinitRegion.comp.spv",
                          reinitPipelineLayout,
                          "smoke reinit region compute pipeline");

  // gridOrigin starts at {0,0,0}; populate every cell in that initial
  // window from solidQueryFn (default: open air) via the same path
  // recenter() uses later, so there's exactly one code path for "give this
  // cell fresh data".
  reinitAll();

  // reinitAll() above only touches the CPU-side solidCellMap (which then
  // gets uploaded on the first update() via solidMapDirty). The float
  // images have no CPU-side mirror anymore, so they need an explicit clear
  // or frame 1 starts from undefined device memory. This is the one call
  // site before update() has ever run, so it needs its own submission -
  // every other clearImages() call happens from inside update()'s shared
  // command buffer (see recenter()).
  clearImagesStandalone();
}

SmokeSys::~SmokeSys() {
  if (context.vulkan.device) {
    context.vulkan.device.destroyPipelineLayout(computePipelineLayout, nullptr);
    context.vulkan.device.destroyPipelineLayout(brushPipelineLayout, nullptr);
    context.vulkan.device.destroyPipelineLayout(reinitPipelineLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(computeSetLayout, nullptr);
    context.vulkan.device.destroyDescriptorSetLayout(dyeImageSetLayout,
                                                     nullptr);
    context.vulkan.device.destroySampler(dyeSampler, nullptr);
  }
}

// ============================== Pipeline helpers ==============================

vk::PipelineLayout
SmokeSys::createComputePipelineLayout(uint32_t pushConstantSize,
                                      const char *debugName) {
  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  pcRange.offset = 0;
  pcRange.size = pushConstantSize;

  vk::PipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.setLayoutCount = 1;
  layoutInfo.pSetLayouts = &computeSetLayout;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pcRange;

  vk::PipelineLayout layout;
  if (context.vulkan.device.createPipelineLayout(&layoutInfo, nullptr,
                                                 &layout) !=
      vk::Result::eSuccess)
    throw std::runtime_error(std::string("failed to create ") + debugName +
                             " pipeline layout!");
  return layout;
}

std::unique_ptr<ComputePipeline>
SmokeSys::makeComputePipeline(const char *shaderPath,
                              vk::PipelineLayout layout,
                              const char *debugName) {
  return std::make_unique<ComputePipeline>(context, shaderPath, layout,
                                           debugName);
}

// ============================== Barriers ==============================

void SmokeSys::barrierComputeToCompute(vk::CommandBuffer cmd) const {
  vk::MemoryBarrier b{vk::AccessFlagBits::eShaderWrite,
                      vk::AccessFlagBits::eShaderRead |
                          vk::AccessFlagBits::eShaderWrite};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader, {}, b, {},
                      {});
}

void SmokeSys::barrierComputeToTransfer(vk::CommandBuffer cmd) const {
  vk::MemoryBarrier b{vk::AccessFlagBits::eShaderWrite,
                      vk::AccessFlagBits::eTransferRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eTransfer, {}, b, {}, {});
}

void SmokeSys::barrierSetupToCompute(vk::CommandBuffer cmd) const {
  vk::MemoryBarrier b{vk::AccessFlagBits::eTransferWrite |
                          vk::AccessFlagBits::eShaderWrite,
                      vk::AccessFlagBits::eShaderRead |
                          vk::AccessFlagBits::eShaderWrite};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer |
                          vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader, {}, b, {},
                      {});
}

void SmokeSys::clearImages(vk::CommandBuffer cmd) {
  debug::beginLabel(context, cmd, "SmokeSys::clearImages",
                    {0.4f, 0.4f, 0.4f, 1.0f});

  vk::ClearColorValue clearValue{};
  clearValue.setFloat32({0.0f, 0.0f, 0.0f, 0.0f});

  vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

  for (Image3D *img : {dyeImage.get(), velocityImage.get(), pressureImage.get(),
                       divergenceImage.get(), targetVelocityImage.get(),
                       targetDyeImage.get(), scratchVelocityImage.get(),
                       scratchDyeImage.get()}) {
    cmd.clearColorImage(*img, vk::ImageLayout::eGeneral, clearValue, range);
  }

  debug::endLabel(context, cmd);
}

void SmokeSys::clearImagesStandalone() {
  vk::CommandBuffer cmd = beginSingleTimeCommands(context);
  clearImages(cmd);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

// ============================== Reinit / recenter ==============================

void SmokeSys::reinitWorldCell(glm::ivec3 worldCell) {
  int idx = localIndex(worldCell);
  solidCellMap[idx] = solidQueryFn(worldCell) ? 1 : 0;
  velocitiesX[idx] = 0.0f;
  velocitiesY[idx] = 0.0f;
  velocitiesZ[idx] = 0.0f;
  smokeMap[idx] = 0.0f;
  pressureMap[idx] = 0.0f;
}

void SmokeSys::reinitSlice(int axis, int worldAxisCoord) {
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
  // reinitWorldCell() just rewrote solidCellMap entries on the CPU only -
  // flag so update() re-uploads before the GPU reads stale solidity data.
  solidMapDirty = true;
}

void SmokeSys::reinitAll() {
  for (int x = 0; x < cellCount.x; x++)
    for (int y = 0; y < cellCount.y; y++)
      for (int z = 0; z < cellCount.z; z++)
        reinitWorldCell(gridOrigin + glm::ivec3(x, y, z));
  solidMapDirty = true;
}

void SmokeSys::clearSlicesGPU(
    vk::CommandBuffer cmd, const std::vector<std::pair<int, int>> &axisSlices) {
  debug::beginLabel(context, cmd, "SmokeSys::clearSlicesGPU",
                    {0.5f, 0.3f, 0.3f, 1.0f});

  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, reinitPipelineLayout,
                         0, computeSet, {});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *reinitRegionPipeline);

  for (const auto &[axis, worldAxisCoord] : axisSlices) {
    int axisB = (axis + 1) % 3;
    int axisC = (axis + 2) % 3;

    // One cell thick along `axis`, full current-window extent along the
    // other two - i.e. exactly the slice reinitSlice() just updated on the
    // CPU side, expressed as an unwrapped world-cell box for the shader.
    glm::ivec3 regionMin{0};
    glm::ivec3 regionSize{1};
    regionMin[axis] = worldAxisCoord;
    regionMin[axisB] = gridOrigin[axisB];
    regionSize[axisB] = cellCount[axisB];
    regionMin[axisC] = gridOrigin[axisC];
    regionSize[axisC] = cellCount[axisC];

    ReinitPushConstants pc{};
    pc.cellCount = glm::ivec4(cellCount, 0);
    pc.regionMin = glm::ivec4(regionMin, 0);
    pc.regionSize = glm::ivec4(regionSize, 0);
    cmd.pushConstants(reinitPipelineLayout, vk::ShaderStageFlagBits::eCompute,
                      0, sizeof(ReinitPushConstants), &pc);

    uint32_t gx = (static_cast<uint32_t>(regionSize.x) + 7) / 8;
    uint32_t gy = (static_cast<uint32_t>(regionSize.y) + 7) / 8;
    uint32_t gz = (static_cast<uint32_t>(regionSize.z) + 7) / 8;
    cmd.dispatch(gx, gy, gz);

    // Slices can be adjacent (e.g. the player drifted several cells in one
    // frame), so their boxes can share edge texels - barrier before the
    // next dispatch touches the same images.
    barrierComputeToCompute(cmd);
  }

  debug::endLabel(context, cmd);
}

void SmokeSys::recenter(vk::CommandBuffer cmd, glm::vec3 playerWorldPos) {
  glm::ivec3 playerCell = glm::ivec3(glm::floor(playerWorldPos / cellSize));

  // Collected across all three axes so the GPU-side clear can go out as one
  // dispatch batch instead of one per exposed slice.
  std::vector<std::pair<int, int>> exposedSlices;

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
      // the old window, so just refresh everything (CPU solidity
      // bookkeeping AND the GPU dye/velocity/pressure/divergence images)
      // instead of sweeping slice-by-slice.
      reinitAll();
      clearImages(cmd);
      continue;
    }

    int exposedCount = std::abs(shift);
    for (int i = 0; i < exposedCount; i++) {
      int worldAxisCoord = shift > 0 ? oldOrigin + n + i : newOrigin + i;
      reinitSlice(axis, worldAxisCoord);
      exposedSlices.emplace_back(axis, worldAxisCoord);
    }
  }

  if (!exposedSlices.empty())
    clearSlicesGPU(cmd, exposedSlices);
}

// ============================== Sampling ==============================

float SmokeSys::sampleTrilinear(const std::vector<float> &field,
                                glm::vec3 worldPos, glm::vec3 sampleOffset) {
  glm::vec3 pos = worldPos - sampleOffset;
  glm::ivec3 i0 = glm::ivec3(glm::floor(pos));
  glm::vec3 t = pos - glm::vec3(i0);

  auto at = [&](int dx, int dy, int dz) {
    return field[localIndex(i0 + glm::ivec3(dx, dy, dz))];
  };

  float c00 = glm::mix(at(0, 0, 0), at(1, 0, 0), t.x);
  float c10 = glm::mix(at(0, 1, 0), at(1, 1, 0), t.x);
  float c01 = glm::mix(at(0, 0, 1), at(1, 0, 1), t.x);
  float c11 = glm::mix(at(0, 1, 1), at(1, 1, 1), t.x);
  float c0 = glm::mix(c00, c10, t.y);
  float c1 = glm::mix(c01, c11, t.y);
  return glm::mix(c0, c1, t.z);
}

// ============================== Brush / laser ==============================

void SmokeSys::applyBrush(vk::CommandBuffer cmd, glm::ivec3 centerCell,
                          float radiusCells, glm::vec3 velocityDelta,
                          float smokeRate) {
  int radiusInt = static_cast<int>(std::ceil(radiusCells));
  int boxSize = radiusInt * 2 + 1;

  BrushPushConstants bpc{};
  bpc.cellCount = glm::ivec4(cellCount, 0);
  bpc.brushMinCell = glm::ivec4(centerCell - glm::ivec3(radiusInt), boxSize);
  bpc.brushCenterAndRadius = glm::vec4(glm::vec3(centerCell), radiusCells);
  bpc.velocityDeltaAndSmoke = glm::vec4(velocityDelta, smokeRate);
  bpc.maxSpeedAndPad = glm::vec4(brushMaxSpeed, 0.0f, 0.0f, 0.0f);

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

void SmokeSys::applyLaser(vk::CommandBuffer cmd, glm::vec3 originWorld,
                          glm::vec3 direction, float radiusCells,
                          glm::vec3 velocityDelta) {
  glm::vec3 dir = glm::normalize(direction);

  // Space samples ~1 radius apart so consecutive spheres overlap slightly
  // and leave no gaps along the beam.
  float stepCells = std::max(radiusCells, 1.0f);

  // The grid is toroidal, so a straight ray re-enters cells it already
  // touched once it's traveled past the local window's extent along the
  // axis it's actually aligned with - going further just repaints the
  // same physical cells, ADDING velocity a second (or third) time since
  // applyBrush is a read-modify-write, not a set. Using the full 3D
  // diagonal here (length(cellCount)) way overshoots that for anything
  // close to axis-aligned - a shot fired straight down one axis would
  // travel ~1.7 laps of that axis's actual period before stopping,
  // double-adding the impulse across most of the beam and destabilizing
  // the sim. Capping at the SHORTEST axis extent guarantees at most one
  // lap along every axis (conservative for diagonal shots - they won't
  // quite reach the far corner - but never re-touches a cell).
  float travelCells =
      static_cast<float>(std::min({cellCount.x, cellCount.y, cellCount.z}));
  int numSteps = static_cast<int>(std::ceil(travelCells / stepCells));

  for (int i = 0; i <= numSteps; i++) {
    glm::vec3 sampleWorld = originWorld + dir * (stepCells * cellSize) *
                                              static_cast<float>(i);
    glm::ivec3 centerCell =
        glm::ivec3(glm::floor(sampleWorld / cellSize));

    applyBrush(cmd, centerCell, radiusCells, velocityDelta, 0.0f);

    // Same barrier every other dispatch in update() uses: brush is a
    // read-modify-write on the velocity/dye storage images, and
    // consecutive spheres along the beam can overlap, so each step's
    // write must be visible before the next step reads the same texels.
    if (i != numSteps)
      barrierComputeToCompute(cmd);
  }
}

// ============================== Update ==============================

SmokeSys::FrameInputs SmokeSys::gatherFrameInputs() {
  FrameInputs in{};
  in.playerPos = context.camera.position;
  in.forward =
      -glm::normalize(glm::vec3(context.camera.inverseViewMatrix[2]));

  in.brushActive =
      glfwGetMouseButton(context.window, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
  if (in.brushActive) {
    glm::vec3 brushCenter = in.playerPos + in.forward * brushDistance;
    in.brushCenterCell = glm::ivec3(glm::floor(brushCenter / cellSize));
  }
  in.brushRadiusCells = brushRadius / cellSize;

  in.laserActive = glfwGetMouseButton(context.window,
                                      GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
  in.laserRadiusCells = laserRadius / cellSize;

  // Player-movement push: derive velocity from the change in position since
  // last frame, rather than trusting a physics-system velocity we don't
  // have access to here. `dt` is the configured fixed sim timestep the rest of
  // this class assumes, not this frame's real wall-clock delta - so this is
  // technically only correct if update() is being called at a matching
  // fixed rate. If SmokeSys::update() is ever called at a variable frame
  // rate, this will over/under-estimate velocity; swap in a real per-frame
  // dt here if one becomes available.
  in.playerVelocity = glm::vec3(0.0f);
  if (havePreviousPlayerPos)
    in.playerVelocity = (in.playerPos - previousPlayerPos) / dt;
  previousPlayerPos = in.playerPos;
  havePreviousPlayerPos = true;

  in.playerCenterCell = glm::ivec3(glm::floor(in.playerPos / cellSize));
  in.playerRadiusCells = playerPushRadius / cellSize;

  return in;
}

std::unique_ptr<Buffer<uint8_t>>
SmokeSys::recenterAndUploadSolidMap(vk::CommandBuffer cmd,
                                    glm::vec3 playerPos) {
  recenter(cmd, playerPos);

  // recenter()'s clearSlicesGPU may have left reinitPipelineLayout's
  // descriptor set bound - rebind before anything below assumes
  // computePipelineLayout, same pattern applyBrush uses after its own
  // differently-laid-out dispatch.
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, computePipelineLayout,
                         0, computeSet, {});

  // --- Only re-upload solid cells when recenter() actually changed them ---
  // The returned buffer needs to outlive this call now: it used to be
  // uploaded via a synchronous copyFromBuffer() that fully executed before
  // returning, so the buffer could be destroyed right after. Now the copy
  // is just recorded here and doesn't actually run until endSingleTimeCommands
  // submits and waits at the bottom of update(), so the caller has to keep
  // this alive until then.
  std::unique_ptr<Buffer<uint8_t>> solidStagingBuffer;
  if (solidMapDirty) {
    debug::beginLabel(context, cmd, "smoke: upload solid cells",
                      {0.6f, 0.6f, 0.6f, 1.0f});
    solidStagingBuffer = std::make_unique<Buffer<uint8_t>>(
        context, vk::BufferUsageFlagBits::eTransferSrc,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        solidCellMap.size());
    solidStagingBuffer->map();
    std::memcpy(solidStagingBuffer->getMappedAddr(), solidCellMap.data(),
                solidCellMap.size() * sizeof(uint8_t));
    solidStagingBuffer->unmap();

    // recordCopyFromBuffer asserts the destination image is already in
    // eTransferDstOptimal - unlike the old copyFromBuffer() (which opened
    // its own single-time command buffer and handled the transition
    // internally), the record version just records the copy and trusts the
    // caller to have the image in the right layout already. solidCellImage
    // otherwise lives in eGeneral the rest of the time (it's read/written
    // as a storage image by the compute shaders), so it has to be
    // transitioned there and back around just this copy.
    //
    // Using recordTransitionLayout() rather than a hand-rolled
    // vk::ImageMemoryBarrier here matters, not just for convenience: Image
    // tracks its current layout in a private `layout` member that only
    // transitionLayout()/recordTransitionLayout() ever update. A manual
    // barrier would move the image's *actual* layout to eTransferDstOptimal
    // without updating that tracked value, so the very next
    // recordCopyFromBuffer() call (or anything else that trusts `layout`)
    // would still assert - the object would think it's still eGeneral.
    solidCellImage->recordTransitionLayout(cmd,
                                           vk::ImageLayout::eTransferDstOptimal);
    solidCellImage->recordCopyFromBuffer(cmd, *solidStagingBuffer);
    solidCellImage->recordTransitionLayout(cmd, vk::ImageLayout::eGeneral);

    solidMapDirty = false;
    debug::endLabel(context, cmd);
  }

  // One barrier covering everything recorded above (recenter()'s clears and
  // the solid-map upload) before the brush/main dispatches below read or
  // write the same images.
  barrierSetupToCompute(cmd);

  return solidStagingBuffer;
}

void SmokeSys::dispatchBrushes(vk::CommandBuffer cmd, const FrameInputs &in) {
  if (in.brushActive) {
    debug::beginLabel(context, cmd, "smoke: brush", {0.9f, 0.5f, 0.1f, 1.0f});
    applyBrush(cmd, in.brushCenterCell, in.brushRadiusCells,
              in.forward * brushStrength * (dt * 60.f),
              brushSmokeRate * (dt * 60.f));
    // Re-bind descriptor set back to computePipelineLayout for divergence pass
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computePipelineLayout, 0, computeSet, {});
    barrierComputeToCompute(cmd);
    debug::endLabel(context, cmd);
  }

  if (in.playerRadiusCells > 0.0f) {
    debug::beginLabel(context, cmd, "smoke: player push",
                      {0.1f, 0.7f, 0.9f, 1.0f});
    // smokeRate = 0: this only perturbs velocity, never adds dye - the
    // player pushing air around shouldn't itself look like a smoke source.
    // *dt for the same reason as the laser fix above: without it, this
    // adds the player's full instantaneous velocity to the air every
    // single frame (not once per second of contact), compounding
    // unboundedly the longer the player stays in one spot moving.
    applyBrush(cmd, in.playerCenterCell, in.playerRadiusCells,
              in.playerVelocity * playerPushStrength * dt, 0.0f);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computePipelineLayout, 0, computeSet, {});
    barrierComputeToCompute(cmd);
    debug::endLabel(context, cmd);
  }

  if (in.laserActive && in.laserRadiusCells > 0.0f) {
    debug::beginLabel(context, cmd, "smoke: laser", {0.9f, 0.1f, 0.9f, 1.0f});
    // *dt: laserStrength is a rate (world units/s of push), not a one-shot
    // impulse magnitude - without this, applyLaser added the FULL
    // laserStrength to velocity on every frame the button was held (not
    // once per second), so holding it down compounded by ~laserStrength
    // every single frame and blew the velocity field up in a fraction of
    // a second regardless of camera movement.
    applyLaser(cmd, in.playerPos, in.forward, in.laserRadiusCells,
              in.forward * laserStrength * dt);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                           computePipelineLayout, 0, computeSet, {});
    barrierComputeToCompute(cmd);
    debug::endLabel(context, cmd);
  }
}

SmokeSys::ComputePushConstants SmokeSys::buildPushConstants() const {
  ComputePushConstants pc{};
  pc.cellSize = cellSize;
  pc.dt = dt;
  pc.cellCount = glm::ivec4(cellCount, 0);
  // Retention fraction after `dt` seconds such that after dyeFadeSeconds/
  // velocityFadeSeconds a cell right at the window edge is down to ~1% of
  // its original value. advect.comp blends this in only near the edge -
  // see edgeFadeCells - and every other pipeline pushes the same struct and
  // just ignores these trailing fields.
  pc.dyeDecay = std::pow(0.01f, dt / std::max(dyeFadeSeconds, 1e-4f));
  pc.velocityDecay =
      std::pow(0.01f, dt / std::max(velocityFadeSeconds, 1e-4f));
  pc.gridOrigin = glm::ivec4(gridOrigin, 0);
  pc.edgeFadeCells = edgeFadeCells;
  pc.vorticityStrength = vorticityStrength;
  return pc;
}

void SmokeSys::dispatchProjection(vk::CommandBuffer cmd,
                                  ComputePushConstants &pc, uint32_t groupsX,
                                  uint32_t groupsY, uint32_t groupsZ) {
  debug::beginLabel(context, cmd, "smoke: divergence", {0.2f, 0.5f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *divergencePipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrierComputeToCompute(cmd);
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: pressure solve",
                    {0.2f, 0.7f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *pressureSolvePipeline);
  for (int iter = 0; iter < pressureIterations; iter++) {
    for (int rb = 0; rb < 2; rb++) {
      pc.rb = rb;
      cmd.pushConstants(computePipelineLayout,
                        vk::ShaderStageFlagBits::eCompute, 0,
                        sizeof(ComputePushConstants), &pc);
      cmd.dispatch(groupsX, groupsY, groupsZ);
      barrierComputeToCompute(cmd);
    }
  }
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: update velocities",
                    {0.2f, 0.9f, 0.5f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *updateVelocitiesPipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrierComputeToCompute(cmd);
  debug::endLabel(context, cmd);
}

void SmokeSys::dispatchVorticity(vk::CommandBuffer cmd,
                                 const ComputePushConstants &pc,
                                 uint32_t groupsX, uint32_t groupsY,
                                 uint32_t groupsZ) {
  // Re-injects small-scale swirl that semi-Lagrangian advection's
  // interpolation smooths away every step (see vorticityConfinement.comp).
  // Writes into targetVelocities (never velocities in place - see that
  // shader's comment on the neighbor-read race that would otherwise
  // cause), then the confined result is copied back into velocityImage on
  // the GPU before advection reads it below.
  debug::beginLabel(context, cmd, "smoke: vorticity confinement",
                    {0.9f, 0.6f, 0.2f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *vorticityPipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrierComputeToTransfer(cmd);
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: apply vorticity",
                    {0.9f, 0.6f, 0.2f, 1.0f});
  {
    vk::ImageCopy velRegion{};
    velRegion.srcSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    velRegion.dstSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
    velRegion.extent = vk::Extent3D{static_cast<uint32_t>(cellCount.x),
                                    static_cast<uint32_t>(cellCount.y),
                                    static_cast<uint32_t>(cellCount.z)};
    cmd.copyImage(*targetVelocityImage, vk::ImageLayout::eGeneral,
                  *velocityImage, vk::ImageLayout::eGeneral, velRegion);
  }
  barrierSetupToCompute(cmd);
  debug::endLabel(context, cmd);
}

void SmokeSys::dispatchAdvection(vk::CommandBuffer cmd,
                                 const ComputePushConstants &pc,
                                 uint32_t groupsX, uint32_t groupsY,
                                 uint32_t groupsZ) {
  // 3 dispatches instead of 1 - see advect.comp/advectBackward.comp/
  // macCormackCombine.comp for the breakdown of forward estimate, reversed
  // resample, and combine+clamp.
  debug::beginLabel(context, cmd, "smoke: advect forward",
                    {0.7f, 0.2f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *advectPipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrierComputeToCompute(cmd);
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: advect backward",
                    {0.7f, 0.3f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *advectBackwardPipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrierComputeToCompute(cmd);
  debug::endLabel(context, cmd);

  debug::beginLabel(context, cmd, "smoke: advect combine",
                    {0.7f, 0.4f, 0.9f, 1.0f});
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *macCormackCombinePipeline);
  cmd.pushConstants(computePipelineLayout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(ComputePushConstants), &pc);
  cmd.dispatch(groupsX, groupsY, groupsZ);
  barrierComputeToTransfer(cmd);
  debug::endLabel(context, cmd);
}

void SmokeSys::copyTargetsBack(vk::CommandBuffer cmd) {
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
}

bool SmokeSys::update(vk::CommandBuffer cmd, float frameTime) {
  dt = glm::clamp(dt, 1.f / 120.f, 1.f / 15.f);
  // Fixed-timestep accumulator: one step per dt of real time, at most one per
  // call, with at most one dt of backlog carried over (so a hitch doesn't
  // cause a burst of catch-up steps).
  simAccumulator = std::min(simAccumulator + frameTime, 2.f * dt);
  if (simAccumulator < dt)
    return false;
  simAccumulator -= dt;

  // Only on stepping frames: previousPlayerPos advances once per step, so
  // playerVelocity = delta / dt is measured over ~dt of real time, matching
  // the fixed rate gatherFrameInputs() has always assumed.
  FrameInputs in = gatherFrameInputs();
  activeSmoke = activeSmoke || in.brushActive;
  activeFluid = activeFluid || in.brushActive || in.laserActive ||
                in.playerRadiusCells > 0.f;
  if (!activeFluid)
    return false; // initialized volumes are already empty; no pressure solve

  debug::beginLabel(context, cmd, "SmokeSys::update", {0.3f, 0.6f, 0.3f, 1.0f});

  // The sim used to run in its own submission followed by a full queue wait,
  // which implicitly ordered it against everything from previous frames.
  // Recorded into the frame's command buffer it needs explicit ordering:
  // previous frame's compute reads of the dye/velocity volumes (transmittance
  // + composite) and previous step's writes must finish before this step
  // writes them. A pipeline barrier's first scope covers everything earlier
  // in submission order on the queue, including prior frames' submits.
  {
    const auto access = vk::AccessFlagBits::eShaderRead |
                        vk::AccessFlagBits::eShaderWrite |
                        vk::AccessFlagBits::eTransferRead |
                        vk::AccessFlagBits::eTransferWrite;
    vk::MemoryBarrier begin{access, access};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader |
                            vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader |
                            vk::PipelineStageFlagBits::eTransfer,
                        vk::DependencyFlags{}, begin, nullptr, nullptr);
  }

  const uint32_t groupsX = (cellCount.x + 7) / 8;
  const uint32_t groupsY = (cellCount.y + 7) / 8;
  const uint32_t groupsZ = (cellCount.z + 7) / 8;

  // recenter()'s clears and the solid-map upload record onto this same
  // command buffer. The staging buffer must stay alive until this frame's
  // GPU work is done - parked per frame-in-flight slot, see the header.
  std::unique_ptr<Buffer<uint8_t>> solidStagingBuffer =
      recenterAndUploadSolidMap(cmd, in.playerPos);

  dispatchBrushes(cmd, in);

  ComputePushConstants pc = buildPushConstants();
  dispatchProjection(cmd, pc, groupsX, groupsY, groupsZ);
  dispatchVorticity(cmd, pc, groupsX, groupsY, groupsZ);
  dispatchAdvection(cmd, pc, groupsX, groupsY, groupsZ);
  copyTargetsBack(cmd);

  // The copies in copyTargetsBack() are transfer writes; the transmittance
  // and composite passes read the dye later in this same command buffer, and
  // the next step reads velocity in compute. (The blocking submit used to
  // make this implicit.)
  {
    vk::MemoryBarrier end{vk::AccessFlagBits::eTransferWrite |
                              vk::AccessFlagBits::eShaderWrite,
                          vk::AccessFlagBits::eShaderRead |
                              vk::AccessFlagBits::eShaderWrite};
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer |
                            vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader,
                        vk::DependencyFlags{}, end, nullptr, nullptr);
  }

  const size_t slot = static_cast<size_t>(context.frameInfo.frameIndex);
  if (pendingSolidStaging.size() <= slot)
    pendingSolidStaging.resize(slot + 1);
  pendingSolidStaging[slot] = std::move(solidStagingBuffer);

  debug::endLabel(context, cmd); // matches "SmokeSys::update"
  ++simStepCount;
  return true;
}

} // namespace vkh
