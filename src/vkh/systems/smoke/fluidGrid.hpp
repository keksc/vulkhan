#pragma once

#include <functional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "../system.hpp"

#include <memory>
#include <vector>

namespace vkh {

// A fixed-size 3D grid that follows the player through an infinite world.
// The grid is periodic (toroidal): local index (lx,ly,lz) represents world
// cell (gridOrigin + (lx,ly,lz)) wrapped modulo cellCount on each axis. When
// the player moves far enough, gridOrigin shifts and only the newly-exposed
// slice(s) are reinitialized - the rest of the data is untouched, nothing is
// physically copied.
//
// Because the domain is periodic, staggered velocity components have the
// SAME element count as cell-centered fields (nx*ny*nz): face x sits between
// cell x-1 and cell x, wrapping around, so there's no "+1" boundary case.
class FluidGrid : public System {
public:
  FluidGrid(EngineContext &context, glm::ivec3 cellCount, float cellSize);
  ~FluidGrid();

  // Called once per frame (before update()) with the player's current world
  // position. Shifts gridOrigin by whole cells when the player drifts too
  // far from grid center, and reinitializes any slices that shift exposes.
  void recenter(glm::vec3 playerWorldPos);

  void update(glm::ivec3 brushCenterCell, bool brushActive,
              float brushRadiusCells, glm::vec3 brushVelocityDelta,
              float brushSmokeRate,
              // Player-movement push: same mechanism as the paint brush,
              // but no dye added - just velocity, so walking through the
              // grid actually disturbs it instead of the air being
              // completely inert everywhere the paint brush hasn't touched.
              glm::ivec3 playerCenterCell = {}, bool playerPushActive = false,
              float playerRadiusCells = 0.0f,
              glm::vec3 playerVelocityDelta = {},
              // Velocity laser: injects velocity along a ray from
              // laserOriginWorld in laserDirection, one brush-sphere per
              // step, out to one full traversal of the grid's local extent.
              // No dye added - same rationale as the player push.
              bool laserActive = false, glm::vec3 laserOriginWorld = {},
              glm::vec3 laserDirection = {}, float laserRadiusCells = 0.0f,
              glm::vec3 laserVelocityDelta = {});

  std::function<bool(glm::ivec3 worldCell)> solidQueryFn = [](glm::ivec3) {
    return false;
  };

  size_t cellTotal() const {
    return static_cast<size_t>(cellCount.x) * cellCount.y * cellCount.z;
  }

  glm::ivec3 cellCount;
  glm::ivec3 gridOrigin{0};
  float cellSize;

  const float density = 1.f;
  const float dt = 1.f / 60.f;

  // How far (in cells) the player may drift from grid center before we
  // recenter. Keep this a few cells so recentering isn't triggered every
  // frame from tiny jitter.
  int recenterThreshold = 8;

private:
  // Wraps a world cell straight to a flat index, with NO origin subtraction.
  // This is deliberate: index(W) = wrap(W) mod cellCount is a fixed function
  // of the world coordinate alone, so a given physical slot always means
  // "every world cell congruent to L mod cellCount", regardless of where
  // gridOrigin currently is. gridOrigin is bookkeeping only - it tracks
  // which one period of world space is currently considered valid, so
  // recenter() knows which world cells just entered that period and need
  // their (possibly stale, from whatever far-away world cell last owned
  // that slot) data reinitialized. It is deliberately NOT part of this
  // formula - subtracting it here would silently reinterpret every slot's
  // data as belonging to a shifted coordinate without actually updating it.
  inline int localIndex(glm::ivec3 worldCell) const {
    glm::ivec3 local;
    local.x = ((worldCell.x % cellCount.x) + cellCount.x) % cellCount.x;
    local.y = ((worldCell.y % cellCount.y) + cellCount.y) % cellCount.y;
    local.z = ((worldCell.z % cellCount.z) + cellCount.z) % cellCount.z;
    return local.x + local.y * cellCount.x +
           local.z * cellCount.x * cellCount.y;
  }

  // Resets solidity/velocity/smoke for a single world cell from scratch
  // (via solidQueryFn), discarding whatever stale data its slot held.
  void reinitWorldCell(glm::ivec3 worldCell);
  // Calls reinitWorldCell for every world cell in the current window whose
  // coordinate along `axis` equals worldAxisCoord (i.e. a full slice).
  void reinitSlice(int axis, int worldAxisCoord);
  // Reinitializes the entire current window - used when a single recenter
  // shifts further than the grid spans, so no data carries over at all.
  void reinitAll();

  void clearImages();

  std::vector<float> velocitiesX, velocitiesY, velocitiesZ;
  std::vector<float> targetVelocitiesX, targetVelocitiesY, targetVelocitiesZ;
  std::vector<float> pressureMap;
  std::vector<float> divergence;
  std::vector<float> smokeMap, targetSmoke;
  std::vector<uint8_t> solidCellMap;

public:
  vk::DescriptorSetLayout dyeImageSetLayout;
  vk::DescriptorSet dyeImageSet;

  struct ComputePushConstants {
    int rb;
    float cellSize;
    float dt;
    // Explicit padding: glm::ivec4 is NOT guaranteed 16-byte aligned by the
    // C++ compiler (that requires GLM's SIMD/aligned gentypes, which aren't
    // enabled here) - without this, cellCount lands at byte offset 12, but
    // every shader's std430 push-constant layout puts ivec3/ivec4 cellCount
    // at offset 16 (16-byte-aligned, per std430 rules for vec3/vec4-sized
    // members). Without this pad, everything from cellCount onward reads
    // 4 bytes off from what the shader expects - cellCount.z silently reads
    // as the always-zero .w padding component instead of the real value,
    // which made every compute dispatch's bounds check (p.z >= cellCount.z)
    // trivially true and return immediately for every invocation.
    int _pad0;
    // ivec4, not ivec3: GLSL's std430 push-constant layout aligns ivec3 to
    // 16 bytes, which glm::ivec3 does NOT reproduce on the C++ side (it's a
    // plain 12-byte, 4-byte-aligned type) - the two sides would silently
    // disagree on this member's offset. ivec4 sidesteps the mismatch; w is
    // unused.
    glm::ivec4 cellCount;
  };

  vk::DescriptorSet computeSet;
  vk::DescriptorSetLayout computeSetLayout;
  vk::PipelineLayout computePipelineLayout;

  std::unique_ptr<ComputePipeline> divergencePipeline;
  std::unique_ptr<ComputePipeline> pressureSolvePipeline;
  std::unique_ptr<ComputePipeline> updateVelocitiesPipeline;
  std::unique_ptr<ComputePipeline> advectPipeline;
  std::unique_ptr<ComputePipeline> brushPipeline;
  vk::PipelineLayout brushPipelineLayout;

  struct BrushPushConstants {
    glm::ivec4 cellCount;
    glm::ivec4 brushMinCell;
    glm::vec4 brushCenterAndRadius;
    glm::vec4 velocityDeltaAndSmoke;
  };

  void applyBrush(vk::CommandBuffer cmd, glm::ivec3 centerCell,
                  float radiusCells, glm::vec3 velocityDelta, float smokeRate);

  // Steps applyBrush along a ray from originWorld in direction, one sphere
  // per step, covering one full traversal of the grid's local extent (the
  // grid is toroidal, so going further than that just revisits the same
  // physical cells). Never adds dye - it's a velocity-only push, like the
  // player push.
  void applyLaser(vk::CommandBuffer cmd, glm::vec3 originWorld,
                  glm::vec3 direction, float radiusCells,
                  glm::vec3 velocityDelta);

  // solidCellMap upload is now conditional - only re-upload after recenter()
  // actually touches cells.
  bool solidMapDirty = true;

  std::unique_ptr<Image3D> dyeImage;
  std::unique_ptr<Image3D> velocityImage;
  std::unique_ptr<Image3D> pressureImage;
  std::unique_ptr<Image3D> solidCellImage;
  std::unique_ptr<Image3D> divergenceImage;
  std::unique_ptr<Image3D> targetVelocityImage;
  std::unique_ptr<Image3D> targetDyeImage;
};

} // namespace vkh
