#pragma once

#include "../../buffer.hpp"
#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "../system.hpp"

#include <functional>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <utility>
#include <vector>

namespace vkh {

// A GPU smoke simulation on a fixed-size 3D grid that follows the player
// through an infinite world. The grid is periodic (toroidal): local index
// (lx,ly,lz) represents world cell (gridOrigin + (lx,ly,lz)) wrapped modulo
// cellCount on each axis. When the player moves far enough, gridOrigin
// shifts and only the newly-exposed slice(s) are reinitialized - the rest
// of the data is untouched, nothing is physically copied.
//
// Because the domain is periodic, staggered velocity components have the
// SAME element count as cell-centered fields (nx*ny*nz): face x sits between
// cell x-1 and cell x, wrapping around, so there's no "+1" boundary case.
//
// Used to be split into SmokeSys (input handling) + FluidGrid (everything
// else) - merged since FluidGrid was the entire simulation and SmokeSys was
// a thin, mostly-empty wrapper around it. Rendering lives in
// PostProcessingSys - see its dye-volume binding. Sampling the resolved
// depth image as a texture from a dedicated graphics pass turned out to
// require either a second rendering instance with no depth attachment (to
// sidestep a feedback loop) or extra extensions this engine doesn't use;
// the postprocessing compute pass already reads depth cleanly with none of
// that, so smoke's raymarch lives there instead.
class SmokeSys : public System {
public:
  SmokeSys(EngineContext &context);
  ~SmokeSys();

  void update();

  // ============================== Tuning ==============================
  // All plain member variables now, so UI/settings can bind straight to
  // them instead of threading values through update()'s parameter list.

  // Paint brush (left mouse button).
  float brushDistance = 6.0f;  // how far in front of the camera smoke spawns
  float brushRadius = 3.0f;    // world units
  float brushStrength = 6.0f;  // velocity imparted along the camera's forward
  float brushSmokeRate = 0.6f; // smoke density added per frame at brush center

  // Hard cap on velocity magnitude after ANY brush write (paint brush,
  // player push, or a laser sphere) - see BrushPushConstants::
  // maxSpeedAndPad for why this exists. Set well above brushStrength/
  // laserStrength's per-hit magnitude (they're multiplied by dt before
  // reaching applyBrush, so a single hit is tiny) but low enough that
  // sustained/repeated hits can't drive advection past what a dt=1/60
  // step on a 1-unit grid can stay stable at. 40 is a starting point -
  // if you still see the sim destabilize with heavy sustained laser use,
  // lower this before reaching for anything else.
  float brushMaxSpeed = 40.0f;

  // Player-movement push: the player's own motion injects velocity into the
  // grid at their position (no dye), so walking through smoke actually
  // disturbs it instead of it being static wallpaper. Set playerPushRadius
  // to 0 to disable this entirely (e.g. while isolating other sources of
  // unwanted initial velocity) - the update() loop skips the push whenever
  // playerRadiusCells (derived from this) is <= 0.
  float playerPushRadius = 0.0f;   // world units around the player
  float playerPushStrength = 1.0f; // multiplier on the player's actual
                                   // world-space velocity (1.0 = 1:1)

  // Velocity laser (right mouse button): a beam of velocity impulses fired
  // from the camera along its forward vector, out to the far side of the
  // grid's local window. No dye - it's meant to stir the smoke, not add to
  // it.
  float laserRadius = 1.5f;    // world units, per-step sphere radius
  float laserStrength = 20.0f; // velocity imparted along the camera's forward

  // How far (in cells) the player may drift from grid center before we
  // recenter. Keep this a few cells so recentering isn't triggered every
  // frame from tiny jitter.
  int recenterThreshold = 8;

  // Exponential fade applied to dye/velocity during advection, but ONLY
  // within edgeFadeCells of the current window's boundary - the interior
  // (where the player and all brush/laser activity actually is) is left
  // completely untouched, full strength, forever. This is deliberately a
  // near-edge effect, not a global one: the goal isn't "smoke slowly dims
  // everywhere", it's "smoke doesn't just vanish/reappear when its cell
  // crosses into the strip that's about to get recycled by recenter()".
  // Since recenter() only triggers once the player drifts recenterThreshold
  // cells from window center, the edge strip is always far from the player
  // (at least n/2 - recenterThreshold cells away), so this fade is never
  // visible near where you're actually looking.
  //
  // Expressed as "time to decay to ~1% of the original value at the very
  // edge" so it's tunable in seconds rather than a raw per-frame
  // multiplier; the shader blends linearly from no decay at
  // edgeFadeCells-away down to this rate right at the boundary.
  float dyeFadeSeconds = 1.5f;
  float velocityFadeSeconds = 1.0f;
  // How many cells wide the near-edge fade zone is. Keep this comfortably
  // smaller than cellCount/2 - recenterThreshold so it never reaches
  // anywhere the player can actually be.
  float edgeFadeCells = 12.0f;

  // Vorticity confinement strength (epsilon) - re-injects small-scale
  // swirl that semi-Lagrangian advection's interpolation damps out every
  // step. 0 disables it entirely. Too high and you'll get visible grid-
  // aligned artifacts/instability; start around 2-6 and tune by eye.
  float vorticityStrength = 0.0f;

  // Optional hook: given a world-space cell coordinate, return whether it's
  // solid (terrain/geometry). Defaults to "always open air" - hook this up
  // to your terrain/collision system, since with an infinite world there's
  // no more implicit border to rely on for containment.
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

  vk::DescriptorSetLayout dyeImageSetLayout;
  vk::DescriptorSet dyeImageSet;

  // Dedicated sampler for dyeImageSet's combined-image-sampler binding -
  // NOT context.vulkan.defaultSampler, which clamps to edge. The dye volume
  // is periodic (see the class comment on gridOrigin/wrapping), so sampling
  // near a wrapped face with a clamping sampler smears instead of
  // continuing across, and defaultSampler's filter mode was never
  // guaranteed to be linear either, which is what made the grid visibly
  // blocky up close.
  vk::Sampler dyeSampler;

  std::unique_ptr<Image3D> dyeImage;
  std::unique_ptr<Image3D> velocityImage;
  std::unique_ptr<Image3D> pressureImage;
  std::unique_ptr<Image3D> solidCellImage;
  std::unique_ptr<Image3D> divergenceImage;
  std::unique_ptr<Image3D> targetVelocityImage;
  std::unique_ptr<Image3D> targetDyeImage;

  // Scratch volumes for MacCormack advection's reversed-then-resampled
  // intermediate (phi_hat_n - see advectBackward.comp/macCormackCombine.
  // comp). Never copied back into velocityImage/dyeImage; purely an error
  // estimate consumed within the same frame's command buffer.
  std::unique_ptr<Image3D> scratchVelocityImage;
  std::unique_ptr<Image3D> scratchDyeImage;

private:
  // ============================== Sim step ==============================

  // Called once per frame (before the main sim dispatches) with the
  // player's current world position. Shifts gridOrigin by whole cells when
  // the player drifts too far from grid center, and reinitializes any
  // slices that shift exposes. Records onto the caller's command buffer
  // instead of submitting its own - see update()'s single command buffer
  // for why: opening a separate beginSingleTimeCommands/endSingleTimeCommands
  // here means a full CPU-blocking GPU sync every time the player moves,
  // on top of the one update() already does for the rest of the sim.
  void recenter(vk::CommandBuffer cmd, glm::vec3 playerWorldPos);

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

  // GPU-side counterpart to reinitSlice/reinitAll: reinitSlice/reinitAll
  // only ever touched the CPU-side vectors below (which nothing reads
  // anymore - see clearImages()), so without this the dye/velocity/
  // pressure/divergence *images*, which are the actual simulation state,
  // never got cleared when a slice was recycled. That's what let old smoke
  // "reappear" on the opposite face of the window after a recenter: the
  // slot's stale GPU data was simply reused as-is. Dispatches
  // reinitRegionPipeline once per exposed slice, zeroing every image for
  // just that slice's cells. Records onto the caller's command buffer -
  // see recenter()'s comment.
  void clearSlicesGPU(vk::CommandBuffer cmd,
                      const std::vector<std::pair<int, int>> &axisSlices);

  // Zeroes every float storage image (dye/velocity/pressure/divergence and
  // their targets) on the GPU. Device-local image memory is NOT guaranteed
  // to start at zero - unlike the old CPU vectors (which zero-init on
  // construction and got re-uploaded every frame), these images are now the
  // only copy of sim state, so without an explicit clear they start as
  // whatever garbage was previously in that memory (including NaN/inf),
  // which silently corrupts the very first frame's brush/advect/render.
  // Records onto the caller's command buffer - see recenter()'s comment.
  void clearImages(vk::CommandBuffer cmd);

  // Standalone wrapper around clearImages(cmd) for the one call site that
  // isn't already inside update()'s command buffer: the constructor, before
  // the render loop (and therefore update()) has run even once.
  void clearImagesStandalone();

  // Pushed to every sim-step dispatch (divergence, pressure solve, update
  // velocities, vorticity, the three advection passes). Moved up here
  // (ahead of the pipeline objects that used to sit between it and the
  // frame-orchestration methods below) purely so those methods can name
  // it in their own signatures - no behavior change.
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

    // Per-frame multipliers (0..1) applied to advected dye/velocity - see
    // dyeFadeSeconds/velocityFadeSeconds above for where these come from.
    // These land right after cellCount, so cellCount MUST stay a full
    // ivec4: std430 only aligns a vec3 to 16 bytes at its own start, it
    // does NOT pad its *size* out to 16 - a plain ivec3 here would let the
    // GLSL compiler pack dyeDecay at byte 28 instead of 32, silently
    // disagreeing with this struct again (same failure mode as the
    // cellCount padding issue above, one field over).
    float dyeDecay;
    float velocityDecay;

    // Explicit pad, same rationale as _pad0 above: dyeDecay/velocityDecay
    // leave us at byte offset 40, but GLSL's std430 rules give the ivec4
    // below a 16-byte alignment (i.e. it'll land at byte 48 in the
    // shader) whether or not anything is written here to reserve that gap.
    // Without this pad, gridOrigin and everything after it would silently
    // read 8 bytes off from what the shader expects.
    float _pad1;
    float _pad2;

    // Current window origin, so advect.comp can work out how far a given
    // cell sits from the window boundary (for the near-edge fade - see
    // dyeFadeSeconds/edgeFadeCells above). w is unused.
    glm::ivec4 gridOrigin;
    // Width, in cells, of the near-edge fade zone - see edgeFadeCells above.
    float edgeFadeCells;

    // Appended at the end: safe to add trailing push-constant fields
    // without touching any earlier offset, and existing shaders that
    // don't declare this field simply never reference past what they do
    // declare (see divergence.comp/updateVelocities.comp truncating the
    // struct early - same pattern). Only vorticityConfinement.comp reads
    // this. See SmokeSys::vorticityStrength above.
    float vorticityStrength;
  };

  // ============================== Pipeline helpers ==============================
  // Every compute dispatch in this class needs a pipeline layout (bound to
  // computeSetLayout, sized for its own push-constant struct) and a
  // pipeline built against that layout. The constructor used to spell out
  // PushConstantRange + PipelineLayoutCreateInfo + createPipelineLayout by
  // hand for each of the three push-constant structs below (~25 lines
  // each, only the struct size and error string differing) - these two
  // helpers are what's left of that.

  // Builds a pipeline layout bound to computeSetLayout with a single
  // compute-stage push-constant range of pushConstantSize bytes. Several
  // pipelines can (and do - see computePipelineLayout) share one of these;
  // callers own the returned handle and are responsible for destroying it
  // in the destructor, same as before.
  vk::PipelineLayout createComputePipelineLayout(uint32_t pushConstantSize,
                                                 const char *debugName);

  // Thin wrapper around ComputePipeline's constructor - exists mainly so
  // every call site below reads as "shader, layout, name" instead of
  // repeating context/ComputePipeline boilerplate.
  std::unique_ptr<ComputePipeline>
  makeComputePipeline(const char *shaderPath, vk::PipelineLayout layout,
                      const char *debugName);

  // ============================== Barriers ==============================
  // update() needs the same handful of barrier shapes over and over
  // between dispatches (and clearSlicesGPU/applyLaser each needed their
  // own copy of the compute-to-compute one) - centralized here instead of
  // being reconstructed as a local lambda/vk::MemoryBarrier at every call
  // site.

  // Storage-image write -> next dispatch's read/write, both compute stage.
  // The everyday barrier between consecutive sim-step dispatches.
  void barrierComputeToCompute(vk::CommandBuffer cmd) const;

  // Storage-image write (compute) -> transfer-stage read (the copyImage
  // calls that bring targetVelocityImage/targetDyeImage back into
  // velocityImage/dyeImage). A compute-to-compute barrier gives the
  // transfer engine no ordering guarantee, so this needs its own dstStage.
  void barrierComputeToTransfer(vk::CommandBuffer cmd) const;

  // Transfer-or-compute write (recenter()'s clears, the solid-map upload)
  // -> compute read/write. Covers the handoff into the main per-frame sim
  // dispatches after any combination of recenter()'s work has run.
  void barrierSetupToCompute(vk::CommandBuffer cmd) const;

  float sampleTrilinear(const std::vector<float> &field, glm::vec3 worldPos,
                        glm::vec3 sampleOffset);

  // Applies a spherical velocity/dye impulse directly on the GPU - the
  // shared primitive behind the paint brush, the player push, and the
  // laser (which is just a chain of these along a ray).
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

  // ============================== Frame orchestration ==============================
  // update() itself is now just these phases called in order - each one
  // used to be an inline block directly in update()'s body (see git
  // history), which made the whole function read as one ~300-line wall
  // regardless of which phase you actually cared about.

  // Everything update() derives from input/camera/player state before it
  // touches the command buffer at all: brush/laser/player-push targets and
  // whether each is currently active this frame.
  struct FrameInputs {
    glm::vec3 playerPos;
    glm::vec3 forward;

    bool brushActive;
    glm::ivec3 brushCenterCell;
    float brushRadiusCells;

    bool laserActive;
    float laserRadiusCells;

    glm::vec3 playerVelocity;
    glm::ivec3 playerCenterCell;
    float playerRadiusCells;
  };
  FrameInputs gatherFrameInputs();

  // recenter()'s clears + the conditional solid-map upload, followed by
  // the one barrier that covers everything either of those recorded. The
  // returned staging buffer (null if the solid map wasn't dirty this
  // frame) must be kept alive by the caller until endSingleTimeCommands
  // actually submits - see the member comment on why this can't just be a
  // local here.
  std::unique_ptr<Buffer<uint8_t>>
  recenterAndUploadSolidMap(vk::CommandBuffer cmd, glm::vec3 playerPos);

  // The three optional velocity/dye sources: paint brush, player-movement
  // push, and laser. Each rebinds computePipelineLayout and inserts a
  // barrier after itself - see applyBrush's own layout-mismatch comment
  // for why the rebind is necessary.
  void dispatchBrushes(vk::CommandBuffer cmd, const FrameInputs &in);

  // Assembles this frame's ComputePushConstants (decay factors, grid
  // origin, etc.) - everything every sim-step dispatch below pushes,
  // varying only the per-dispatch pieces (currently just `rb`, set inside
  // dispatchProjection's Jacobi loop).
  ComputePushConstants buildPushConstants() const;

  // divergence -> 40 iterations of red-black Gauss-Seidel pressure solve
  // -> velocity update. Mutates pc.rb in place across the Jacobi loop.
  void dispatchProjection(vk::CommandBuffer cmd, ComputePushConstants &pc,
                          uint32_t groupsX, uint32_t groupsY,
                          uint32_t groupsZ);

  // Vorticity confinement, writing into targetVelocityImage, then copied
  // back into velocityImage on the GPU before advection reads it.
  void dispatchVorticity(vk::CommandBuffer cmd, const ComputePushConstants &pc,
                         uint32_t groupsX, uint32_t groupsY,
                         uint32_t groupsZ);

  // MacCormack advection's three dispatches (forward estimate, reversed
  // resample, combine+clamp) - see advect.comp/advectBackward.comp/
  // macCormackCombine.comp.
  void dispatchAdvection(vk::CommandBuffer cmd, const ComputePushConstants &pc,
                         uint32_t groupsX, uint32_t groupsY,
                         uint32_t groupsZ);

  // Copies targetVelocityImage/targetDyeImage back into
  // velocityImage/dyeImage, GPU-side, no CPU round trip.
  void copyTargetsBack(vk::CommandBuffer cmd);

  std::vector<float> velocitiesX, velocitiesY, velocitiesZ;
  std::vector<float> targetVelocitiesX, targetVelocitiesY, targetVelocitiesZ;
  std::vector<float> pressureMap;
  std::vector<float> divergence;
  std::vector<float> smokeMap, targetSmoke;
  std::vector<uint8_t> solidCellMap;

  // solidCellMap upload is now conditional - only re-upload after recenter()
  // actually touches cells.
  bool solidMapDirty = true;

  vk::DescriptorSet computeSet;
  vk::DescriptorSetLayout computeSetLayout;
  vk::PipelineLayout computePipelineLayout;

  std::unique_ptr<ComputePipeline> divergencePipeline;
  std::unique_ptr<ComputePipeline> pressureSolvePipeline;
  std::unique_ptr<ComputePipeline> updateVelocitiesPipeline;
  std::unique_ptr<ComputePipeline> vorticityPipeline;
  // MacCormack advection is now 3 dispatches sharing computePipelineLayout
  // (advectPipeline does step 1 - see advect.comp's header comment):
  //   advectPipeline          -> phi_hat        (targetVelocities/targetDye)
  //   advectBackwardPipeline  -> phi_hat_n       (scratchVelocities/scratchDye)
  //   macCormackCombinePipeline -> final result (targetVelocities/targetDye,
  //     overwriting phi_hat, ready for update()'s existing copy-back step)
  std::unique_ptr<ComputePipeline> advectPipeline;
  std::unique_ptr<ComputePipeline> advectBackwardPipeline;
  std::unique_ptr<ComputePipeline> macCormackCombinePipeline;
  std::unique_ptr<ComputePipeline> brushPipeline;
  vk::PipelineLayout brushPipelineLayout;

  struct BrushPushConstants {
    glm::ivec4 cellCount;
    glm::ivec4 brushMinCell;
    glm::vec4 brushCenterAndRadius;
    glm::vec4 velocityDeltaAndSmoke;
    // x = max velocity magnitude allowed after this brush write (yzw
    // unused/padding). Every applyBrush call - paint brush, player push,
    // and each step of the laser's sphere chain - writes velocity as a
    // plain curVel+delta with no cap, so repeated hits (holding the laser
    // down, standing in one spot) accumulate speed without bound. Past a
    // certain magnitude, MacCormack advection's backward trace starts
    // sampling many cells away per step instead of a fraction of one -
    // and since the grid is toroidal that never goes "out of bounds", it
    // just smears whatever dye already exists across the whole domain in
    // a frame or two. Clamping the WRITE (not just the delta) means the
    // persistent field itself can never exceed this, regardless of how
    // many times or how fast it's hit.
    glm::vec4 maxSpeedAndPad;
  };

  // Used by clearSlicesGPU to wipe an arbitrary axis-aligned box of cells
  // unconditionally - unlike BrushPushConstants' boxSize, regionSize is
  // per-axis since an exposed slice is thin along one axis and full-extent
  // along the other two.
  struct ReinitPushConstants {
    glm::ivec4 cellCount;  // xyz used
    glm::ivec4 regionMin;  // xyz = unwrapped world-cell min corner
    glm::ivec4 regionSize; // xyz = box dimensions along each axis
  };

  std::unique_ptr<ComputePipeline> reinitRegionPipeline;
  vk::PipelineLayout reinitPipelineLayout;

  // Previous frame's player position, used to derive an instantaneous
  // world-space velocity (playerPos - previousPlayerPos) / dt. Starts
  // uninitialized-but-marked so the very first frame doesn't inject a huge
  // spurious velocity from an uninitialized/zero previous position.
  glm::vec3 previousPlayerPos{};
  bool havePreviousPlayerPos = false;
};

} // namespace vkh
