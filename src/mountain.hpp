#pragma once

#include "rng.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/image.hpp"
#include "vkh/scene.hpp"
#include "vkh/systems/entity/entities.hpp"

#include <glm/glm.hpp>

#include <cstdint>
#include <functional>
#include <memory>
#include <random>
#include <vector>

namespace vkh::mountain {

// Returns a fresh random value each call, via a function-local RNG seeded
// once from std::random_device. Used only as MountainCreateInfo::seed's
// default member initializer (below), so that a plain
// MountainCreateInfo{} produces a different mountain every time it's
// constructed (i.e. every call to generate() with default settings),
// instead of the same fixed terrain forever. Pass an explicit seed
// instead when you actually want reproducibility (e.g. loading a save,
// or comparing tuning changes on the same terrain).
inline uint32_t randomMountainSeed() {
  static std::uniform_int_distribution<uint32_t> dist;
  return dist(rng);
}

struct MountainCreateInfo {
  // Pure, thread-safe world-height transform, applied after noise/erosion.
  // Extends every terrain/lake/grass query consistently (e.g. volcano cones).
  std::function<float(glm::vec2, float)> heightModifier;
  std::function<bool(glm::vec3)> lakeAllowed;
  // Disable when using streamed TerrainSys: still exports the heightmap,
  // lakes, and trees, but skips the unused finite mesh and its GPU upload.
  bool buildMesh{true};
  // Number of vertices along each side of the grid. Higher values give more
  // detail but cost more triangles (resolution.x * resolution.y quads).
  // NOTE: this must scale roughly with worldSize (keep worldSize/resolution
  // - i.e. vertex spacing - fairly constant) or erosion octaves will start
  // aliasing into noise; see the Nyquist guard in evalHeight().
  glm::uvec2 resolution{512u, 512u};

  // World-space footprint of the mesh on the XZ plane, centered on the
  // origin.
  glm::vec2 worldSize{400.f, 400.f};

  // World XZ the heightmap/lake grid is centred on (only used by
  // findLakesInTile; generate() always builds the mesh at the origin).
  glm::vec2 worldCenter{0.f, 0.f};

  // Wavelength, in absolute world units, of the base landscape's largest
  // noise octave. This is intentionally independent of worldSize: growing
  // worldSize while holding this fixed tiles more peaks/valleys across a
  // bigger landscape, rather than just stretching a single mountain.
  float baseFeatureWorldSize{100.f};

  // Number of octaves layered into the base landscape's turbulent noise,
  // each higher-frequency and lower-amplitude than the last - ported from
  // the shader's HEIGHT_OCTAVES. More octaves add finer bumps/dips to the
  // base shape itself (independent of, and evaluated before, the erosion
  // filter's own octaves).
  uint32_t baseOctaves{3u};

  // Frequency multiplier applied to the base landscape noise each octave
  // - ported from the shader's HEIGHT_LACUNARITY.
  float baseLacunarity{2.f};

  // Amplitude multiplier applied to the base landscape noise each octave
  // - ported from the shader's HEIGHT_GAIN. The shader's demo keeps this
  // quite low (0.1), so the base landscape stays a single broad shape
  // with only subtle finer bumps, leaving the more pronounced ridge/
  // valley carving to the erosion filter rather than the base noise
  // itself.
  float baseGain{0.1f};

  // Height (Y) of the lowest and highest points of the base landscape,
  // before erosion is layered on top.
  float baseHeight{0.f};
  float peakHeight{40.f};

  // Number of erosion octaves. Each octave adds a smaller, higher frequency
  // layer of gullies/ridges on top of the previous one, replacing (not
  // adding to) the running result wherever its mask permits.
  uint32_t octaves{6};

  // Overall strength of the erosion effect. Height is normalized to
  // [0,1] overall, so erosionStrength must stay small enough that
  // gullyWeight * erosionStrength doesn't approach/exceed ~0.3-0.5, or a
  // single octave can swing height across the entire normalized range and
  // saturate the final clamp into flat-topped mesas with sheer walls. The
  // source shader's own demo uses erosionStrength=0.22 with
  // gullyWeight=0.5 (NOT a 1/gullyWeight ratio, despite the "pointy
  // peaks" section's phrasing suggesting they scale inversely) - keep new
  // values in that neighborhood and adjust by small increments.
  float erosionStrength{0.22f};

  // Frequency multiplier applied to the stripe pattern each octave (>1
  // makes each successive octave finer). The post typically uses ~2.
  float lacunarity{2.f};

  // Amplitude multiplier applied to each octave's gully magnitude before
  // it's blended in (<1 makes each successive octave contribute less).
  float gain{0.55f};

  // Scales down each octave's gully height/gradient contribution before
  // blending, to keep mountain peaks pointy rather than rounded off by
  // gully noise - see "Pointy peaks" in the blog post. Does not need a
  // strict inverse pairing with erosionStrength; see that field's comment.
  float gullyWeight{0.5f};

  // Detail parameter from the "Stacked fading" section: controls how
  // strongly finer octaves are restricted to steep slopes only, vs. being
  // allowed to add detail more broadly. Higher = more restricted/detailed
  // look, lower = smoother/broader.
  float detail{1.5f};

  // Overall spatial scale of the erosion effect. Together with cellScale,
  // sets the initial Phacelle stripe frequency: freq0 = 1 / (erosionScale
  // * cellScale). Unlike the source shader (where this also multiplied
  // strength, since both lived in the same small UV-space unit system),
  // this port keeps erosionScale and erosionStrength independent, since
  // here erosionScale is a world-space size (tens of units) while
  // erosionStrength controls height magnitude in the [0,1] normalized
  // range - multiplying them would blow height contributions far past
  // that range.
  float erosionScale{0.15f};

  // Size of the Phacelle noise cells relative to erosionScale, without
  // affecting stripe width. Values close to 1 usually work well; smaller
  // gives grainier gullies, larger gives longer unbroken (but eventually
  // chaotic/curved) gullies. Not well suited to animation/modulation -
  // see the Phacelle noise function's own comments in the shader.
  float cellScale{0.7f};

  // Degree of normalization applied in the Phacelle noise, between 0 and
  // 1. Needed for the erosion filter's magnitude consistency, but values
  // too close to 1 can make ridges/creases meet at unnaturally loopy
  // points.
  float normalization{0.5f};

  // Separate rounding control of ridges vs. creases, ported verbatim from
  // the shader's EROSION_ROUNDING:
  //  x: rounding of ridges.
  //  y: rounding of creases.
  //  z: multiplier applied when rounding the *base landscape's* input
  //     mask (compensates if the base landscape noise is much lower
  //     frequency than the largest gully octave).
  //  w: multiplier applied to the rounding of each subsequent gully
  //     octave after the first - set equal to lacunarity for consistent
  //     rounding across all octaves.
  glm::vec4 rounding{0.1f, 0.f, 0.1f, 2.f};

  // Control over how far away from ridges/creases the erosion takes
  // effect, ported from the shader's EROSION_ONSET (x/y components only -
  // the shader's z/w only affect the ridge-map output, which this mesh
  // port doesn't produce):
  //  x: onset used on the base landscape's input mask. UNLIKE onset.y,
  //     this multiplies the *raw* base-landscape gradient magnitude
  //     (which is small - roughly 0.02-0.05 - for a broad, smooth base
  //     bump at the default baseFeatureWorldSize/baseHeight amplitude),
  //     not a pre-normalized [0,1] noise value. The shader's own default
  //     of 1.25 assumes a much higher-gradient FractalNoise base and, if
  //     reused here unchanged, keeps this mask closed almost everywhere
  //     except a thin ring around the base bump's steepest slope - which
  //     reads as "erosion barely visible / terrain too smooth" no matter
  //     how high erosionStrength is set. Retune this (roughly
  //     1/max-base-gradient) if you change baseFeatureWorldSize or
  //     baseHeight/peakHeight's implied amplitude - use the
  //     combiMaskMean/Max debug stats (VKH_MOUNTAIN_DEBUG_PRINT) to check
  //     whether the mask is actually opening up.
  //  y: onset used on each gully octave's mask - this one *does* operate
  //     on a pre-normalized [0,1]-ish value (sloping = |phacelle.y|), so
  //     it doesn't need the same rescaling.
  glm::vec2 onset{20.f, 1.25f};

  // Control over the assumed slope direction used to orient gullies,
  // ported from the shader's EROSION_ASSUMED_SLOPE:
  //  x: an assumed slope magnitude to (partially) override the actual
  //     base-landscape slope direction.
  //  y: blend amount (0 = use actual slope, 1 = use assumed slope) - can
  //     look better than the real slope since the final eroded terrain
  //     may be shaped quite differently than the un-eroded base.
  glm::vec2 assumedSlope{0.7f, 1.f};

  // Optional overall height bias applied after erosion, ported from the
  // shader's TERRAIN_HEIGHT_OFFSET. Defaults to (0, 0), i.e. no bias -
  // unlike the shader demo, which uses this to sink the whole terrain
  // down by default.
  //  x: offset (-1..1), scaled by the accumulated erosion magnitude.
  //  y: blend toward "only raise valleys / only lower peaks" behavior
  //     (replaces x with the negated running fade target).
  glm::vec2 heightOffset{0.f, 0.f};

  // Slope angle (degrees from horizontal) at and beyond which the surface
  // material is fully "steep rock", regardless of altitude - i.e. a
  // near-vertical cliff face reads as bare rock even at grass or snow
  // altitude, matching how real slopes shed vegetation/snow cover once
  // they're too steep to hold onto. Slopes flatter than this blend
  // smoothly toward the normal altitude-based bands (rock/dirt/grass/
  // rock/snow). 45 degrees is a reasonable default; lower it for a
  // rockier-looking mountain overall, raise it to let grass/snow persist
  // on steeper slopes.
  float rockSlopeAngleDeg{45.f};

  // Seeds all of the noise functions above (base landscape, Phacelle
  // erosion octaves). Defaults to a fresh random value per construction
  // (see randomMountainSeed()) so you get a different mountain each time
  // without doing anything - set this explicitly instead if you need the
  // same terrain again (e.g. loading a save).
  uint32_t seed{randomMountainSeed()};

  // --- Lake placement ---------------------------------------------------
  // Lakes are found via priority-flood depression filling (see
  // generate()'s outLakes block): flooding inward from the map edges
  // computes, for every cell, the true water level it could be flooded to
  // before draining out to the boundary. Cells below that level are
  // genuinely, physically enclosed depressions - connected groups of them
  // become lakes, each sitting at its own settled water level, which is
  // what gives lakes on the same mountain different heights for free.
  // This replaced an earlier per-cell percentile/slope/neighbor-margin
  // heuristic that kept rejecting visibly-enclosed basins because those
  // independent thresholds don't actually correspond to "does this hold
  // water" for an irregular basin shape.
  bool generateLakes{true};

  // Minimum number of connected grid cells a depression needs to qualify
  // as a lake - filters out single-texel noise dips that aren't a real
  // basin.
  uint32_t lakeMinCells{24};

  // Minimum depth (in normalized [0,1] height units, water level minus
  // the depression's lowest point) for a depression to qualify - filters
  // out trivial, barely-there puddles.
  float lakeMinDepth{0.01f};

  // World-space distance the water level is lowered below the basin's
  // settled spill level, so the lake surface sits visibly below the
  // shoreline instead of exactly at it (which would leave the water
  // plane clipping/z-fighting against the rim terrain).
  float lakeShoreMargin{0.15f};

  // --- Tree placement -----------------------------------------------------
  // Trees are scattered via jittered-grid dart-throwing over the same
  // height/slope grid used for the heightmap and grass-suitability texture -
  // deliberately not true Poisson-disc (no neighbor search needed), which
  // costs a bit of spacing uniformity but is negligible for a forest, since
  // real tree spacing is irregular anyway.
  bool generateTrees{true};

  // Normalized [0,1] altitude band (matches Vertex::uv.x / MountainHeightmap's
  // r channel after normalization) trees are allowed in - keeps them off
  // valley floors right at the waterline and above the treeline.
  float treeMinAltitude{0.05f};
  float treeMaxAltitude{0.55f};

  // Trees avoid slopes steeper than this - same normalized slope metric as
  // rockSlopeAngleDeg/slopeNorm above (0 = flat, 1 = at-or-beyond
  // rockSlopeAngleDeg from horizontal).
  float treeMaxSlopeNorm{0.35f};

  // World-space jittered-grid cell size - roughly the average spacing
  // between trees.
  float treeMinSpacing{3.5f};

  // [0,1] probability a candidate that already passed the altitude/slope
  // checks actually spawns a tree - lower values open up clearings instead
  // of a solid forest wall.
  float treeDensity{0.6f};

  // Number of tree mesh variants the caller has available; each
  // TreePlacement gets a variant index in [0, treeVariantCount) so a mixed
  // forest doesn't read as one model copy-pasted everywhere.
  uint32_t treeVariantCount{3};
};

// Vertex spacing of the finite mountain mesh (the larger of the two axes, the
// same value evalHeight()'s octave limit derives from). This is the single
// source of truth for "LOD 0 detail": use it as TerrainSys::CreateInfo::
// lod0CellSize so LOD 0 chunks, the lake builder and the heightmap all see the
// same ground. Chunks over lakes are pinned to it at every LOD.
inline float terrainCellSize(const MountainCreateInfo &ci) {
  const glm::uvec2 r = glm::max(ci.resolution, glm::uvec2(2u));
  const glm::vec2 c = ci.worldSize / glm::vec2(r - glm::uvec2(1u));
  return glm::max(c.x, c.y);
}

// One scattered tree - the caller (main.cpp) turns each of these into a
// vkh::TreeSys::TreePlacement (position/yaw/scale carry over directly;
// `variant` selects which of TreeSys::addVariant()'s registered tree
// species to use - see main.cpp's tree-placement block).
struct TreePlacement {
  glm::vec3 position{};
  float yaw{}; // radians, random facing around Y
  float scale{1.f};
  uint32_t variant{0};
};

// One flood-filled lake basin found in a generated mountain's heightmap -
// see MountainCreateInfo's lake* fields for how these are found.
struct Lake {
  glm::vec3 worldCenter{}; // XZ = bounding-box center, Y = flat water level
  glm::vec2 worldSize{};   // XZ extents of the basin's bounding box

  // Which cells of a maskResolution.x by maskResolution.y grid spanning
  // this bounding box actually belong to this lake's flood-filled basin -
  // true means "part of the lake, render water here", false means "inside
  // the bounding box but outside the basin's actual (often winding, non-
  // convex) shape". Indexed [z * maskResolution.x + x].
  //
  // This exists because worldSize above is ONLY a bounding box - a
  // winding canyon lake's actual footprint can be a small fraction of its
  // bounding box's area, and two separate basins' bounding boxes can
  // overlap even when their real shapes never touch. Building a solid
  // rectangular water surface straight from worldSize used to mean two
  // independent water surfaces (different Y levels, independent mirrored
  // reflection cameras) could physically overlap - see WaterCreateInfo::
  // cellMask in water.hpp, which consumes this directly to build a mesh
  // matching the true footprint instead.
  //
  // One exclusion cell per terrain-aligned water quad, derived after the
  // final water level is settled. Used by streamed grass and trees; water
  // rendering uses meshXZ/meshIndices directly.
  glm::uvec2 maskResolution{0, 0};
  std::vector<bool> cellMask;

  // Water surface mesh, built from the same flood fill as the basin but on a
  // fine lattice (about one terrain cell). World-space XZ points, four indices
  // per quad (v0, v1, v2, v3), and a per-vertex depth. Only the connected
  // water is meshed, including complete quads along its shore. Dry vertices
  // remain on the terrain lattice, burying outer edges beneath the bank.
  // meshDepth is the water depth (waterY - bed height, clamped to 0);
  // WaterSys fades wave displacement to 0 toward the shore.
  // Y is worldCenter.y.
  std::vector<glm::vec2> meshXZ;
  std::vector<float> meshDepth;
  // Lattice cell size in world units (average of the X and Z spacing).
  float meshCellSize{1.5f};
  std::vector<uint32_t> meshIndices;
};

// Builds a heightmap-based mountain mesh implementing the erosion
// technique described by Rune Skovbo Johansen in "Fast and Gorgeous
// Erosion Filter"
// (https://blog.runevision.com/2026/03/fast-and-gorgeous-erosion-filter.html).
// mountain.cpp's evalHeight() is a direct CPU port of that post's
// Shadertoy GLSL - specifically the `PhacelleNoise` and `ErosionFilter`
// functions - evaluated once per mesh vertex instead of per pixel, with
// the raymarched/rendering parts of the shader (which only apply to a
// screen-space heightfield preview) left out.
//
// Ported faithfully: Phacelle noise (cell-blended directional stripe
// noise with pivot jitter, normalized cos/sin pair re-normalized via the
// scale-then-clamp trick), the multi-octave erosion filter with the fade
// approach (per-octave fade-target/mask blending for crisp peaks *and*
// valleys), stacked fading (mask carried forward via pow_inv/detail),
// separate ridge/crease rounding controls (MountainCreateInfo::rounding),
// onset falloff controls (MountainCreateInfo::onset), assumed-slope gully
// orientation (MountainCreateInfo::assumedSlope), and pointy peaks
// (gullyWeight/erosionStrength compensation).
//
// Not ported: the ridge-map output (used in the shader for water
// drainage/tree placement) and the shader's optional
// frequency-approach/comparison-slider paths, none of which affect the
// core height result this mesh needs.
//
// The surface is colored via a small altitude-x-slope gradient texture
// (rock -> dirt -> grass -> snow bands per altitude, further biased
// toward bare rock as slope steepens) sampled through vertex UV: UV.x is
// normalized altitude, UV.y is normalized slope steepness (see
// MountainCreateInfo::rockSlopeAngleDeg). This keeps cliffs reading as
// exposed rock regardless of their altitude, rather than picking up
// grass/snow bands that would only make sense on a gentle slope at that
// height.

// A small texture other systems (e.g. ProceduralGrassSys) can sample to
// place things on the mountain's surface without re-running the erosion
// filter themselves. Format is R32G32_SFLOAT:
//  r: world-space height (Y) at this XZ position - already includes
//     baseHeight/heightRange, so consumers can use it directly as a
//     world-space Y coordinate with no further scaling.
//  g: grass suitability in [0,1], computed from the exact same
//     normalized-altitude/slope values that drive the color texture's
//     grass band (see mountain.cpp's computeGrassSuitability) - 1 where
//     the surface would read as grass-colored, fading to 0 toward rock/
//     dirt/snow altitudes or steep slopes. This is a heuristic match to
//     the color texture, not a shared source of truth - if you retune the
//     color texture's altitude/slope bands, revisit this too.
//
// ASSUMPTION: texel rows are written top-to-bottom with row 0 at
// worldMin.y (i.e. V=0 is the -Z edge of the covered area), matching a
// standard top-left-origin image layout - if the engine's image/sampler
// setup treats V=0 as the opposite edge, sample with V flipped.
struct MountainHeightmap {
  std::shared_ptr<Image2D> image;
  glm::vec2 worldMin{};  // world-space XZ of texel (0, 0)
  glm::vec2 worldSize{}; // world-space size covered (matches
                         // MountainCreateInfo::worldSize)
};

// If outHeightmap is non-null, also builds a MountainHeightmap alongside
// the mesh (from the same underlying height/slope evaluation, so the two
// are always in sync) and writes it there.
//
// If outLakes is non-null and createInfo.generateLakes is set, also
// flood-fills the same height grid for suitable basins (see
// MountainCreateInfo's lake* fields) and appends one Lake per basin found -
// use generateLakeMesh() to turn each into a renderable flat water patch.
//
// If outTrees is non-null and createInfo.generateTrees is set, scatters
// trees over the same height/slope grid the mesh and heightmap are built
// from (see MountainCreateInfo's tree* fields), rejecting candidates that
// fall inside a lake basin from outLakes if that's also provided - pass
// both so trees never spawn standing in water.
std::shared_ptr<Scene<EntitySys::Vertex>>
generate(EngineContext &context, vk::DescriptorSetLayout texturesSetLayout,
         const MountainCreateInfo &createInfo = {},
         MountainHeightmap *outHeightmap = nullptr,
         std::vector<Lake> *outLakes = nullptr,
         std::vector<TreePlacement> *outTrees = nullptr);

// Builds a small flat quad mesh covering a Lake's bounding box at its
// water level - a simple stand-in water surface (solid tinted color, not
// animated/reflective like WaterSys's ocean) suitable for the small,
// mostly-static bodies of water mountain basins produce. Returns one mesh
// per call; the caller adds it to the scene like any other entity.
std::shared_ptr<Scene<EntitySys::Vertex>>
generateLakeMesh(EngineContext &context,
                 vk::DescriptorSetLayout texturesSetLayout, const Lake &lake);

// World-space height (Y) of the mountain at an XZ position, for streaming
// terrain (TerrainSys). `cellSize` is the vertex spacing the sample will be
// shown at: erosion octaves finer than ~3 * cellSize are dropped (Nyquist
// guard in evalHeight), so the SAME point has a slightly different height at
// different cell sizes. Lake meshes are built with terrainCellSize(); callers
// that must match them (chunks over a lake, see TerrainSys::setLakeZones) pass
// that value instead of their own spacing. Pure and thread-safe: the returned
// function owns a copy of createInfo.
std::function<float(glm::vec2 worldXZ, float cellSize)>
makeHeightFn(const MountainCreateInfo &createInfo);

// Lake basins in tile (tx, tz): the createInfo.worldSize square centred on
// (tx, tz) * worldSize. Same detection as generate() but on a half-resolution
// grid. Pure and thread-safe (run it async). A basin touching a tile edge
// counts as draining and is not reported.
std::vector<Lake> findLakesInTile(const MountainCreateInfo &createInfo,
                                  int32_t tx, int32_t tz);

// Cheap approximation of makeHeightFn for visibility tests: drops erosion
// octaves finer than ~3 * cellSize, so it is several times cheaper but can be
// off by a few metres. Thread-safe.
std::function<float(glm::vec2 worldXZ)>
makeCoarseHeightFn(const MountainCreateInfo &createInfo, float cellSize);

} // namespace vkh::mountain
