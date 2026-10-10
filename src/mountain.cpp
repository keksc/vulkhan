#include "mountain.hpp"

#include "vkh/sceneBuilder.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/packing.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <future>
#include <limits>
#include <queue>
#include <thread>
#include <utility>
#include <vector>

namespace vkh::mountain {

namespace {

// ---------------------------------------------------------------------
// Small deterministic hash, used to place a random pivot point inside each
// noise cell. Only the pivot *position* is jittered per cell, not the
// stripe direction - all cells in a given evaluation share the same
// incoming gradient direction, which is what makes it valid to postpone
// the tangent-direction multiply until after blending (see "Normalized
// gullies" in the blog post).
// ---------------------------------------------------------------------
glm::vec2 hash2(glm::vec2 p, uint32_t seed) {
  p = glm::vec2(glm::dot(p, glm::vec2(127.1f, 311.7f)) + seed * 0.0001f,
                glm::dot(p, glm::vec2(269.5f, 183.3f)) + seed * 0.0003f);
  glm::vec2 s(std::sin(p.x) * 43758.5453f, std::sin(p.y) * 43758.5453f);
  return glm::vec2(s.x - std::floor(s.x), s.y - std::floor(s.y)) * 2.0f - 1.0f;
}

// Scalar counterpart of hash2, used as the per-lattice-corner value for
// the base landscape's noise (below).
float hash1(glm::vec2 p, uint32_t seed) {
  float h = glm::dot(p, glm::vec2(127.1f, 311.7f)) + seed * 0.0001f;
  float s = std::sin(h) * 43758.5453f;
  return s - std::floor(s);
}

// ---------------------------------------------------------------------
// Base-landscape noise with analytic derivatives, standing in for the
// shader's `noised` (used inside its FractalNoise, but only referenced as
// living in the shader's separate, unshared "Common" tab - not published
// in what was pasted here, so this isn't a byte-for-byte port of it).
// This is the standard technique it's almost certainly built on: 2D value
// noise, quintic-interpolated so the value *and* its derivative are both
// continuous across cell boundaries (a plain linear/cosine interpolation
// would give a discontinuous derivative, visible as faceting).
//
// Returns (value, dValue/dx, dValue/dy). Both value and derivative are
// remapped to be zero-centered (value roughly in [-1, 1]) to match how
// the shader's FractalNoise output is used (e.g. fadeTarget expects a
// signed, peak/valley-centered signal).
// ---------------------------------------------------------------------
glm::vec3 valueNoiseDeriv(glm::vec2 p, uint32_t seed) {
  glm::vec2 i = glm::floor(p);
  glm::vec2 f = glm::fract(p);

  float a = hash1(i + glm::vec2(0.f, 0.f), seed);
  float b = hash1(i + glm::vec2(1.f, 0.f), seed);
  float c = hash1(i + glm::vec2(0.f, 1.f), seed);
  float d = hash1(i + glm::vec2(1.f, 1.f), seed);

  // Quintic smoothstep and its derivative (Perlin's improved curve),
  // so the noise is C1-continuous across cell boundaries.
  glm::vec2 u = f * f * f * (f * (f * 6.f - 15.f) + 10.f);
  glm::vec2 du = 30.f * f * f * (f * (f - 2.f) + 1.f);

  float value = a + (b - a) * u.x + (c - a) * u.y + (a - b - c + d) * u.x * u.y;
  glm::vec2 deriv =
      du * (glm::vec2(b - a, c - a) + (a - b - c + d) * glm::vec2(u.y, u.x));

  return glm::vec3(value * 2.f - 1.f, deriv.x * 2.f, deriv.y * 2.f);
}

// Direct analog of the shader's FractalNoise: sums several octaves of the
// noise above, each higher-frequency and lower-amplitude than the last.
// Derivatives are scaled by the per-octave frequency (chain rule for
// noise(p*nf)) exactly as the shader's `vec3(1.0, nf, nf)` multiplier
// does.
glm::vec3 fractalNoiseDeriv(glm::vec2 p, float freq, uint32_t octaves,
                            float lacunarity, float gain, uint32_t seed) {
  glm::vec3 n(0.f);
  float nf = freq;
  float na = 1.f;
  for (uint32_t i = 0; i < octaves; ++i) {
    glm::vec3 s = valueNoiseDeriv(p * nf, seed + i * 977u);
    n += s * na * glm::vec3(1.f, nf, nf);
    na *= gain;
    nf *= lacunarity;
  }
  return n;
}

// Utility snippets given verbatim (as pseudocode) in the "Fade approach"
// and "Stacked fading" sections of the blog post.
float inverseLerp(float a, float b, float v) { return (v - a) / (b - a); }

float easeOut(float t) {
  float v = 1.f - glm::clamp(t, 0.f, 1.f);
  return 1.f - v * v;
}

float powInv(float t, float power) {
  return 1.f - std::pow(1.f - glm::clamp(t, 0.f, 1.f), power);
}

// Grass suitability in [0,1], from the same normalized-altitude/slope
// values used to pick a spot on the color texture (see
// buildTerrainTexture's altitude columns and slope rows below). Peaks at
// the grass column's altitude (hNorm == 0.5, matching column index 2 of
// 5) and at zero slope, fading out toward the dirt/rock altitudes and
// toward the moderate-slope row. This is a standalone heuristic re-tuned
// to *look* consistent with the color texture, not a shared calculation
// with it - if the color bands move, this needs to be revisited too.
float computeGrassSuitability(float hNorm, float slopeNorm) {
  // Altitude: full suitability within +-0.15 of the grass column (0.5),
  // linearly fading to 0 by +-0.35 (roughly the dirt/rock column
  // neighbors either side).
  float altitudeScore =
      1.f - glm::clamp((std::abs(hNorm - 0.5f) - 0.15f) / 0.2f, 0.f, 1.f);
  // Slope: full suitability on flat ground, fading to 0 by slopeNorm=0.5
  // (roughly the boundary between the gentle and moderate texture rows) -
  // notably tighter than rockSlopeAngleDeg itself, since real grass gives
  // up on a slope well before it's steep enough to count as a cliff face.
  float slopeScore = 1.f - glm::smoothstep(0.15f, 0.5f, slopeNorm);
  return glm::clamp(altitudeScore * slopeScore, 0.f, 1.f);
}

// smooth_start / safe_normalize, given verbatim in the "Erosion function"
// section of the shader.
float smoothStart(float t, float smoothing) {
  if (t >= smoothing)
    return t - 0.5f * smoothing;
  return 0.5f * t * t / smoothing;
}

glm::vec2 safeNormalize(glm::vec2 n) {
  float l = glm::length(n);
  return (std::abs(l) > 1e-10f) ? (n / l) : n;
}

constexpr float kTau = 6.28318530717959f;

// Direct port of the shader's PhacelleNoise. Iterates the 4x4 neighborhood
// of cell pivots (indices -1..2, matching the shader's loop bounds - the
// asymmetric range is intentional: the pivot jitter is only ever up to 0.5
// units off-center, so the 4x4 window is exactly what's needed to
// guarantee every pivot that could be closest to `p` is included), blends
// a cosine/sine pair per cell via a bell-shaped weight, and returns the
// blended, magnitude-normalized wave together with the (already
// freq/tau-scaled) side direction used to compute derivatives.
//
// Returns a vec4: xy = normalized (cos, sin) pair; zw = sideDir, which
// the caller multiplies onto the sine component to get the gradient of
// the cosine (chain rule), matching how the shader uses `phacelle.zw`.
glm::vec4 phacelleNoise(glm::vec2 p, glm::vec2 normDir, float freq,
                        float offset, float normalization, uint32_t seed) {
  glm::vec2 sideDir = glm::vec2(-normDir.y, normDir.x) * freq * kTau;
  offset *= kTau;

  glm::vec2 pInt = glm::floor(p);
  glm::vec2 pFrac = glm::fract(p);
  glm::vec2 phaseDir(0.f);
  float weightSum = 0.f;

  for (int i = -1; i <= 2; ++i) {
    for (int j = -1; j <= 2; ++j) {
      glm::vec2 gridOffset{float(i), float(j)};
      glm::vec2 gridPoint = pInt + gridOffset;
      glm::vec2 randomOffset = hash2(gridPoint, seed) * 0.5f;
      glm::vec2 vectorFromCellPoint = pFrac - gridOffset - randomOffset;

      float sqrDist = glm::dot(vectorFromCellPoint, vectorFromCellPoint);
      float weight = std::exp(-sqrDist * 2.f);
      // Subtract 0.01111 so the weight is exactly 0 at distance 1.5,
      // avoiding subtle grid-line artifacts - see the shader comment.
      weight = glm::max(0.f, weight - 0.01111f);
      weightSum += weight;

      float waveInput = glm::dot(vectorFromCellPoint, sideDir) + offset;
      phaseDir += glm::vec2(std::cos(waveInput), std::sin(waveInput)) * weight;
    }
  }

  glm::vec2 interpolated =
      weightSum > 1e-8f ? phaseDir / weightSum : glm::vec2(0.f);
  float magnitude = glm::length(interpolated);
  magnitude = glm::max(1.f - normalization, magnitude);

  return glm::vec4(interpolated / magnitude, sideDir);
}

// Result of running the full multi-octave erosion filter at one point.
struct ErosionResult {
  glm::vec3 delta{0.f};  // (height, dHdx, dHdy) delta added by erosion
  float magnitude{0.f};  // sum of per-octave strengths actually applied
  float fadeTarget{0.f}; // running fade-target value, post-loop
};

// Direct port of the shader's ErosionFilter. `heightAndSlope` is (height,
// dHdx, dHdy) of the base landscape at this point; `fadeTarget` should be
// -1 at valleys and +1 at peaks (overshoot is fine). `octaves` is passed
// in already capped by the caller's Nyquist guard.
ErosionResult erosionFilter(glm::vec2 p, glm::vec3 heightAndSlope,
                            float fadeTarget, float strength, float gullyWeight,
                            float detail, glm::vec4 rounding, glm::vec2 onset,
                            glm::vec2 assumedSlope, float scale,
                            uint32_t octaves, float lacunarity, float gain,
                            float cellScale, float normalization,
                            uint32_t seed) {
  // NOTE: the shader multiplies strength by scale here too
  // ("strength *= scale;"). That's fine in the shader's native UV-space
  // units, where scale (e.g. 0.15) and strength (e.g. 0.22) are both
  // small, similarly-scaled numbers. In this world-space port, scale
  // (erosionScale) is deliberately a large number (e.g. 30) chosen to get
  // a sane stripe *frequency* for the mesh's actual size - multiplying
  // that into strength as well would blow height contributions 30x past
  // the [0,1] range and saturate the final clamp into mesas/cliffs. So
  // strength and scale are kept independent here; erosionStrength alone
  // controls height magnitude.
  fadeTarget = glm::clamp(fadeTarget, -1.f, 1.f);

  glm::vec3 inputHeightAndSlope = heightAndSlope;
  float freq = 1.f / (scale * cellScale);
  float slopeLength = glm::max(
      glm::length(glm::vec2(heightAndSlope.y, heightAndSlope.z)), 1e-10f);
  float magnitude = 0.f;
  float roundingMult = 1.f;

  float roundingForInput = glm::mix(rounding.y, rounding.x,
                                    glm::clamp(fadeTarget + 0.5f, 0.f, 1.f)) *
                           rounding.z;
  float combiMask =
      easeOut(smoothStart(slopeLength * onset.x, roundingForInput * onset.x));

  // Assumed-slope blend for the direction that orients the first octave's
  // gullies (and is carried forward octave to octave below).
  glm::vec2 actualSlope(heightAndSlope.y, heightAndSlope.z);
  glm::vec2 gullySlope = glm::mix(
      actualSlope, actualSlope / slopeLength * assumedSlope.x, assumedSlope.y);

  ErosionResult result;

  for (uint32_t i = 0; i < octaves; ++i) {
    glm::vec4 phacelle =
        phacelleNoise(p * freq, safeNormalize(gullySlope), cellScale, 0.25f,
                      normalization, seed + i * 977u);
    // Multiply with freq since p was multiplied with freq; negate since
    // gullySlope points down.
    glm::vec2 phacelleDeriv = glm::vec2(phacelle.z, phacelle.w) * -freq;

    float sloping = std::abs(phacelle.y);
    gullySlope +=
        glm::sign(phacelle.y) * phacelleDeriv * strength * gullyWeight;

    glm::vec3 gullies(phacelle.x, phacelle.y * phacelleDeriv.x,
                      phacelle.y * phacelleDeriv.y);
    glm::vec3 fadedGullies = glm::mix(glm::vec3(fadeTarget, 0.f, 0.f),
                                      gullies * gullyWeight, combiMask);
    heightAndSlope += fadedGullies * strength;
    magnitude += strength;

    fadeTarget = fadedGullies.x;

    float roundingForOctave =
        glm::mix(rounding.y, rounding.x,
                 glm::clamp(phacelle.x + 0.5f, 0.f, 1.f)) *
        roundingMult;
    float newMask =
        easeOut(smoothStart(sloping * onset.y, roundingForOctave * onset.y));
    combiMask = powInv(combiMask, detail) * newMask;

    strength *= gain;
    freq *= lacunarity;
    roundingMult *= rounding.w;
  }

  result.delta = heightAndSlope - inputHeightAndSlope;
  result.magnitude = magnitude;
  result.fadeTarget = fadeTarget;
  return result;
}

// Evaluates the eroded height at a world-space XZ position, following the
// post's "fade approach" + "stacked fading": each octave's gully is
// blended against a running fade-target/mask via mix(), replacing the
// running result rather than summing octaves, so that finer octaves can't
// disturb the ridges/creases already committed by coarser ones.
float evalHeight(glm::vec2 pos, const MountainCreateInfo &info,
                 float cellSizeOverride = 0.f) {
  // Turbulent base landscape, direct analog of the shader's
  // `FractalNoise(pHeight, HEIGHT_FREQUENCY, HEIGHT_OCTAVES,
  // HEIGHT_LACUNARITY, HEIGHT_GAIN)`. baseFeatureWorldSize is the
  // absolute world-space wavelength of the *largest* octave and is
  // intentionally not scaled by worldSize - so growing worldSize tiles
  // more peaks/valleys across a bigger landscape instead of just
  // stretching a single bump.
  //
  // This matters beyond just "more detail": using a single smooth,
  // low-frequency bump here (as an earlier version of this function did)
  // gives the gradient a nearly identical direction over huge contiguous
  // areas, which makes the erosion gullies - whose orientation is driven
  // by this gradient - run parallel across wide swaths of terrain instead
  // of branching and curving locally. That reads as "stretched" rather
  // than organic. Real multi-octave turbulent noise gives every region
  // its own locally-varying slope direction, which is what the erosion
  // filter actually needs to produce natural-looking branching gullies.
  float baseFreq = 1.f / glm::max(info.baseFeatureWorldSize, 1.f);
  glm::vec3 baseNoise =
      fractalNoiseDeriv(pos, baseFreq, info.baseOctaves, info.baseLacunarity,
                        info.baseGain, info.seed);
  // Rescale the roughly [-1, 1] noise value into [0, 1] height space
  // (matching the shader's "n = n * 0.5 + vec3(0.5, 0, 0)"); the
  // derivative scales by the same 0.5 factor.
  float baseH = baseNoise.x * 0.5f + 0.5f;
  glm::vec2 baseGrad(baseNoise.y * 0.5f, baseNoise.z * 0.5f);

  // Input fade target: -1 at valleys, +1 at peaks (inverse_lerp from the
  // "The fade approach" section).
  float fadeTarget = inverseLerp(0.f, 1.f, baseH) * 2.f - 1.f;

  // Nyquist guard: once a gully octave's stripe wavelength gets close to
  // the vertex spacing, it can no longer be resolved by the grid and
  // instead of gullies you get aliased, chaotic salt-and-pepper noise -
  // this is a major contributor to terrain reading as "noisy" rather than
  // mountain-like, especially once worldSize grows without resolution
  // growing to match. We cap the octave count (rather than looping and
  // breaking, since ErosionFilter's octave loop is now self-contained)
  // once a stripe wavelength would need more detail than ~3 vertices can
  // show.
  glm::uvec2 gridRes = glm::max(info.resolution, glm::uvec2(2u));
  glm::vec2 cellSize = info.worldSize / glm::vec2(gridRes - glm::uvec2(1u));
  float cellMax = cellSizeOverride > 0.f ? cellSizeOverride
                                         : glm::max(cellSize.x, cellSize.y);
  float maxSafeFreq = 1.f / (3.f * cellMax);

  float freq0 = 1.f / (info.erosionScale * info.cellScale);
  uint32_t safeOctaves = 0;
  {
    float freq = freq0;
    for (; safeOctaves < info.octaves; ++safeOctaves) {
      if (freq > maxSafeFreq)
        break;
      freq *= info.lacunarity;
    }
  }

  ErosionResult erosion =
      erosionFilter(pos, glm::vec3(baseH, baseGrad.x, baseGrad.y), fadeTarget,
                    info.erosionStrength, info.gullyWeight, info.detail,
                    info.rounding, info.onset, info.assumedSlope,
                    info.erosionScale, safeOctaves, info.lacunarity, info.gain,
                    info.cellScale, info.normalization, info.seed);

  // Shader's TERRAIN_HEIGHT_OFFSET: an optional overall bias, scaled by
  // how much erosion magnitude was actually applied at this point, with
  // an option to blend toward "only raise valleys / only lower peaks"
  // behavior instead of a flat shift.
  float offset =
      glm::mix(info.heightOffset.x, -erosion.fadeTarget, info.heightOffset.y) *
      erosion.magnitude;

  float eroded = baseH + erosion.delta.x + offset;

  // NOTE: intentionally not clamped to [0,1] here. The source shader's
  // Heightmap() never clamps its returned `eroded` value either - it only
  // clamp01()s the *debug/ridge-map channels* it packs into a texture.
  // Clamping the actual height flattens the small fraction of vertices
  // where erosion pushes just past 0 or 1 into hard mesas/cliffs (visible
  // as the residual clampedLow/clampedHigh percentages in the debug
  // stats). generate() already separately clamps just the texture UV
  // (see vert.uv below), which is the one place clamping is needed -
  // sampling the gradient texture with an out-of-range altitude, not the
  // mesh's actual geometry, is the only thing that would misbehave.
  if (info.heightModifier) {
    const float range = info.peakHeight - info.baseHeight;
    return (info.heightModifier(pos, info.baseHeight + eroded * range) -
            info.baseHeight) / range;
  }
  return eroded;
}

// Builds a small altitude-x-slope terrain-color texture: columns are
// altitude bands (rock (low) -> dirt -> grass -> rock -> snow (high), 5
// stops, same as before), rows are slope-steepness bands (flat -> gentle
// -> moderate -> steep, 4 stops). Vertex UV.x encodes normalized altitude,
// UV.y encodes normalized slope steepness (see
// MountainCreateInfo::rockSlopeAngleDeg), so bilinear sampling blends both
// axes at once - a steep cliff at "grass altitude" samples toward the
// steep row's rock color instead of green, while a flat area at the same
// altitude still reads as grass.
//
// LIMITATION - reflectivity: this texture only carries base color (RGB) +
// alpha, with alpha used as an inverse-roughness/"ice-like" mask (see
// Stop::reflectivity below) rather than true PBR roughness. Snow coverage
// and its reflectivity both taper off as slope increases (steep faces
// shed snow), which is handled here by the row data, not by any
// shader-side slope logic.
//
// Stop values below are specified in linear space (i.e. "what color do I
// actually want this to look like"), then gamma-encoded to bytes here,
// matching the convention the engine's existing white-fallback texture
// uses (image.hpp's ImageCreateInfo_color path): entities.frag manually
// converts sampled texture values from gamma to linear via pow(2.2), which
// means textures must be uploaded as Unorm with pre-encoded (pow(1/2.2))
// bytes rather than relying on a hardware sRGB format to do that decode
// automatically (using Srgb here would cause the shader to linearize an
// already-linear value a second time).
//
// ASSUMPTION: rows are written top-to-bottom in the returned buffer with
// row 0 (flat) first, matching a standard top-left-origin image layout
// with V=0 at the first row - if the engine's sampler/image loader treats
// V=0 as the *bottom* row instead, the slope axis will read backwards
// (steep-looking terrain on flat ground and vice versa); flip the `rows`
// array order here if so.
std::vector<uint8_t> buildTerrainTexture(glm::uvec2 &outSize) {
  struct Stop {
    glm::vec3 linearColor; // 0..1, linear space
    uint8_t reflectivity;  // 0..255, alpha channel: how "ice-like" /
                           // reflective this stop should read as.
  };
  // 4 rows (flat -> steep), 5 columns per row (low -> high altitude).
  static constexpr std::array<std::array<Stop, 5>, 4> rows{{
      // Flat: full band progression, as the original single-row texture.
      {{
          {{0.14f, 0.10f, 0.07f}, 0},   // dark rock / scree at the base
          {{0.20f, 0.13f, 0.06f}, 0},   // dirt
          {{0.08f, 0.20f, 0.05f}, 0},   // grass
          {{0.30f, 0.30f, 0.30f}, 0},   // bare rock near the peak
          {{0.90f, 0.92f, 0.95f}, 255}, // snow cap - fully reflective
      }},
      // Gentle slope: slightly rockier/dirtier, thinner and less
      // reflective snow.
      {{
          {{0.16f, 0.12f, 0.08f}, 0},
          {{0.22f, 0.15f, 0.08f}, 0},
          {{0.15f, 0.22f, 0.08f}, 0},
          {{0.33f, 0.32f, 0.30f}, 0},
          {{0.85f, 0.87f, 0.90f}, 200},
      }},
      // Moderate slope: rock-dominant even at low/mid altitude, only
      // sparse grass tint, patchy low-reflectivity snow.
      {{
          {{0.20f, 0.16f, 0.12f}, 0},
          {{0.24f, 0.18f, 0.12f}, 0},
          {{0.28f, 0.24f, 0.18f}, 0},
          {{0.35f, 0.34f, 0.32f}, 0},
          {{0.55f, 0.55f, 0.58f}, 120},
      }},
      // Steep/vertical: bare rock across the entire altitude range - real
      // cliffs shed both vegetation and snow - with only a faint
      // rock/snow mix even at the highest altitude column.
      {{
          {{0.22f, 0.19f, 0.16f}, 0},
          {{0.23f, 0.20f, 0.17f}, 0},
          {{0.25f, 0.22f, 0.18f}, 0},
          {{0.32f, 0.31f, 0.29f}, 0},
          {{0.45f, 0.46f, 0.48f}, 40},
      }},
  }};

  uint32_t cols = static_cast<uint32_t>(rows[0].size());
  uint32_t rowCount = static_cast<uint32_t>(rows.size());
  outSize = glm::uvec2(cols, rowCount);

  std::vector<uint8_t> pixels(static_cast<size_t>(cols) * rowCount * 4);
  for (uint32_t r = 0; r < rowCount; ++r) {
    for (uint32_t c = 0; c < cols; ++c) {
      const Stop &stop = rows[r][c];
      glm::vec3 encoded = glm::pow(glm::clamp(stop.linearColor, 0.f, 1.f),
                                   glm::vec3(1.f / 2.2f));
      size_t idx = (static_cast<size_t>(r) * cols + c) * 4;
      pixels[idx + 0] = static_cast<uint8_t>(encoded.r * 255.0f + 0.5f);
      pixels[idx + 1] = static_cast<uint8_t>(encoded.g * 255.0f + 0.5f);
      pixels[idx + 2] = static_cast<uint8_t>(encoded.b * 255.0f + 0.5f);
      pixels[idx + 3] = stop.reflectivity;
    }
  }
  return pixels;
}

} // namespace

// Priority-flood lake detection on a gridX x gridZ height grid (normalized
// heights) covering createInfo.worldSize centred on createInfo.worldCenter.
static void detectLakes(const MountainCreateInfo &createInfo,
                        const std::vector<float> &heights, uint32_t gridX,
                        uint32_t gridZ, std::vector<Lake> *outLakes) {
  const glm::vec2 halfSize = createInfo.worldSize * 0.5f;
  const float heightRange = createInfo.peakHeight - createInfo.baseHeight;
  auto worldPosAt = [&](uint32_t ix, uint32_t iz) {
    float u = static_cast<float>(ix) / static_cast<float>(gridX - 1);
    float v = static_cast<float>(iz) / static_cast<float>(gridZ - 1);
    return createInfo.worldCenter +
           glm::vec2(glm::mix(-halfSize.x, halfSize.x, u),
                     glm::mix(-halfSize.y, halfSize.y, v));
  };
  {
    // Priority-flood depression filling (the standard terrain/hydrology
    // technique for this - equivalent to LeetCode's "Trapping Rain Water
    // II"): flood inward from the map's edge, and for every cell compute
    // the height water would need to reach for it to escape to the
    // boundary along the *cheapest* route out. A cell whose terrain sits
    // below that computed level is genuinely, physically enclosed - no
    // percentile/slope/rim-margin heuristics needed, because this
    // directly answers "does this cell hold water" rather than
    // approximating it with independent per-cell thresholds (which is
    // what the previous version of this function did, and why it kept
    // rejecting basins that were visibly enclosed - the heuristic didn't
    // actually match what "enclosed" means for an irregular basin shape).
    const size_t cellCount = heights.size();
    std::vector<float> waterLevel(cellCount, std::numeric_limits<float>::max());
    std::vector<uint8_t> settled(cellCount, 0);

    using PQEntry = std::pair<float, uint32_t>; // (waterLevel, cellIdx)
    std::priority_queue<PQEntry, std::vector<PQEntry>, std::greater<>> pq;

    for (uint32_t iz = 0; iz < gridZ; ++iz) {
      for (uint32_t ix = 0; ix < gridX; ++ix) {
        if (iz == 0 || iz == gridZ - 1 || ix == 0 || ix == gridX - 1) {
          size_t idx = static_cast<size_t>(iz) * gridX + ix;
          waterLevel[idx] = heights[idx];
          settled[idx] = 1;
          pq.emplace(heights[idx], static_cast<uint32_t>(idx));
        }
      }
    }

    static const int32_t dx[4] = {-1, 1, 0, 0};
    static const int32_t dz[4] = {0, 0, -1, 1};

    while (!pq.empty()) {
      auto [curLevel, idx] = pq.top();
      pq.pop();
      uint32_t cx = idx % gridX;
      uint32_t cz = idx / gridX;

      for (int n = 0; n < 4; ++n) {
        int32_t nx = static_cast<int32_t>(cx) + dx[n];
        int32_t nz = static_cast<int32_t>(cz) + dz[n];
        if (nx < 0 || nz < 0 || nx >= static_cast<int32_t>(gridX) ||
            nz >= static_cast<int32_t>(gridZ))
          continue;
        size_t nIdx = static_cast<size_t>(nz) * gridX + nx;
        if (settled[nIdx])
          continue;
        settled[nIdx] = 1;
        // This cell can't hold water below whichever is higher: its own
        // terrain, or the level of the cheapest path found so far - it
        // can never be lower than the ground it sits on, and it can
        // never drain below the pour point that got us here.
        float nLevel = std::max(heights[nIdx], curLevel);
        waterLevel[nIdx] = nLevel;
        pq.emplace(nLevel, static_cast<uint32_t>(nIdx));
      }
    }

    // Any cell where the computed water level sits above its own terrain
    // is a genuinely enclosed depression by however much the difference
    // is - group connected depressed cells into basins.
    std::vector<uint8_t> visited(cellCount, 0);
    // Reused per-component membership lookup for the gather-based mask
    // rasterization below (see the `inBasin` comment further down) -
    // allocated once here rather than per-lake, and cleared back to 0 for
    // just this component's `members` after each component is processed
    // (accepted or not) so it stays all-zero going into the next one.
    std::vector<uint8_t> inBasin(cellCount, 0);
    std::vector<uint32_t> stack;
    uint32_t componentsFound = 0;
    uint32_t rejectedTooSmall = 0;
    uint32_t rejectedTooShallow = 0;
    uint32_t accepted = 0;

    for (uint32_t iz = 0; iz < gridZ; ++iz) {
      for (uint32_t ix = 0; ix < gridX; ++ix) {
        size_t startIdx = static_cast<size_t>(iz) * gridX + ix;
        if (visited[startIdx])
          continue;
        float startDepth = waterLevel[startIdx] - heights[startIdx];
        if (startDepth <= 1e-5f)
          continue; // dry land - not part of any depression

        stack.clear();
        stack.push_back(static_cast<uint32_t>(startIdx));
        visited[startIdx] = 1;
        ++componentsFound;

        float minSpillLevel = waterLevel[startIdx];
        float minFloorHeight = heights[startIdx];
        float minX = std::numeric_limits<float>::max();
        float maxX = std::numeric_limits<float>::lowest();
        float minZ = std::numeric_limits<float>::max();
        float maxZ = std::numeric_limits<float>::lowest();
        uint32_t memberCount = 0;
        // Every cell belonging to this component, kept (not just
        // aggregated into min/max) so it can be rasterized into a
        // footprint mask below once the final bounding box is known -
        // see Lake::cellMask's comment for why the bounding box alone
        // isn't enough.
        std::vector<uint32_t> members;

        while (!stack.empty()) {
          uint32_t idx = stack.back();
          stack.pop_back();
          uint32_t cx = idx % gridX;
          uint32_t cz = idx / gridX;

          minSpillLevel = std::min(minSpillLevel, waterLevel[idx]);
          minFloorHeight = std::min(minFloorHeight, heights[idx]);
          glm::vec2 xz = worldPosAt(cx, cz);
          minX = std::min(minX, xz.x);
          maxX = std::max(maxX, xz.x);
          minZ = std::min(minZ, xz.y);
          maxZ = std::max(maxZ, xz.y);
          ++memberCount;
          members.push_back(idx);
          inBasin[idx] = 1;

          for (int n = 0; n < 4; ++n) {
            int32_t nx = static_cast<int32_t>(cx) + dx[n];
            int32_t nz = static_cast<int32_t>(cz) + dz[n];
            if (nx < 0 || nz < 0 || nx >= static_cast<int32_t>(gridX) ||
                nz >= static_cast<int32_t>(gridZ))
              continue;
            size_t nIdx = static_cast<size_t>(nz) * gridX + nx;
            if (visited[nIdx])
              continue;
            float nDepth = waterLevel[nIdx] - heights[nIdx];
            if (nDepth <= 1e-5f)
              continue; // dry - not part of this (or any) depression
            visited[nIdx] = 1;
            stack.push_back(static_cast<uint32_t>(nIdx));
          }
        }

        // Physically, a connected flooded region settles to its single
        // lowest spill point, not whatever level each cell individually
        // computed on the way there - use the minimum across the
        // component as the actual final lake surface.
        if (memberCount < createInfo.lakeMinCells) {
          ++rejectedTooSmall;
          for (uint32_t memberIdx : members)
            inBasin[memberIdx] = 0;
          continue;
        }
        if (minSpillLevel - minFloorHeight < createInfo.lakeMinDepth) {
          ++rejectedTooShallow;
          for (uint32_t memberIdx : members)
            inBasin[memberIdx] = 0;
          continue;
        }

        ++accepted;
        // The extents above come from member cell CENTRES, so the real shore
        // can lie up to one grid cell beyond them. Without this padding the
        // water mesh ends in a straight, axis-aligned cut there, leaving
        // lakebed below the water level uncovered. The padded area is trimmed
        // back by the flood-filled water mesh below and the depth test.
        // At least ~4 terrain cells: the fine lattice must contain the whole
        // true shore, otherwise its border clips the water in a straight cut.
        const float padMin = 4.f * terrainCellSize(createInfo);
        const float padX = std::max(
            createInfo.worldSize.x / static_cast<float>(gridX - 1), padMin);
        const float padZ = std::max(
            createInfo.worldSize.y / static_cast<float>(gridZ - 1), padMin);
        minX -= padX;
        maxX += padX;
        minZ -= padZ;
        maxZ += padZ;
        Lake lake{};
        float waterY = createInfo.baseHeight + minSpillLevel * heightRange -
                       createInfo.lakeShoreMargin;
        lake.worldCenter =
            glm::vec3((minX + maxX) * 0.5f, waterY, (minZ + maxZ) * 0.5f);
        lake.worldSize = glm::vec2(maxX - minX, maxZ - minZ);
        if (createInfo.lakeAllowed && !createInfo.lakeAllowed(lake.worldCenter)) {
          for (uint32_t memberIdx : members) inBasin[memberIdx] = 0;
          continue;
        }

        float boxWidth = maxX - minX;
        float boxDepth = maxZ - minZ;

        // --- Water surface mesh --------------------------------------------
        // Refine the coarse basin on the rendered terrain's lattice, keeping
        // only connected water. Reuse this footprint for vegetation exclusion.
        {
          // Match the terrain's vertex lattice and triangle diagonal exactly.
          // Dry perimeter vertices stay in place, hiding the mesh boundary
          // under the bank instead of cutting water at a grid-aligned contour.
          const float surfaceCell = terrainCellSize(createInfo);
          minX = std::floor(minX / surfaceCell) * surfaceCell;
          minZ = std::floor(minZ / surfaceCell) * surfaceCell;
          maxX = std::ceil(maxX / surfaceCell) * surfaceCell;
          maxZ = std::ceil(maxZ / surfaceCell) * surfaceCell;
          const uint32_t NX =
              static_cast<uint32_t>(std::lround((maxX - minX) / surfaceCell)) +
              1;
          const uint32_t NZ =
              static_cast<uint32_t>(std::lround((maxZ - minZ) / surfaceCell)) +
              1;
          // Keep the previous per-lake sampling budget. Oversized basins are
          // omitted rather than fitted to a coarse grid that exposes shores.
          if (static_cast<size_t>(NX) * NZ > 384u * 384u) {
            for (uint32_t memberIdx : members)
              inBasin[memberIdx] = 0;
            continue;
          }
          boxWidth = maxX - minX;
          boxDepth = maxZ - minZ;
          lake.worldSize = glm::vec2(boxWidth, boxDepth);
          lake.worldCenter.x = (minX + maxX) * 0.5f;
          lake.worldCenter.z = (minZ + maxZ) * 0.5f;
          // Shore slack lets the water run a little under the rim so the
          // terrain mesh's own interpolation never leaves a gap at the edge
          // (the depth test hides the part that is underground).
          // 0.3 (was 0.1): the terrain mesh interpolates linearly between its
          // vertices, so on curved/steep shores it can sit a few decimetres
          // below the true height the contour was taken from.
          constexpr float kShoreSlack = 0.3f;
          float threshold = waterY + kShoreSlack;

          std::vector<float> hTex(static_cast<size_t>(NX) * NZ,
                                  std::numeric_limits<float>::quiet_NaN());
          auto heightTex = [&](uint32_t tx, uint32_t tz) {
            float &c = hTex[static_cast<size_t>(tz) * NX + tx];
            if (std::isnan(c)) {
              glm::vec2 p(minX + tx * surfaceCell, minZ + tz * surfaceCell);
              // Explicit LOD 0 detail: this is the surface the finest terrain
              // chunks (and, via TerrainSys::setLakeZones, every chunk over
              // the lake) are built from.
              c = createInfo.baseHeight +
                  evalHeight(p, createInfo, surfaceCell) * heightRange;
            }
            return c;
          };
          auto nearestGrid = [](float world, float centerOff, float halfExtent,
                                float worldExtent, uint32_t count) {
            float u = (world - centerOff + halfExtent) / worldExtent;
            int32_t g = static_cast<int32_t>(
                std::lround(u * static_cast<float>(count - 1)));
            return static_cast<uint32_t>(
                std::clamp(g, 0, static_cast<int32_t>(count - 1)));
          };

          std::vector<uint8_t> wet(static_cast<size_t>(NX) * NZ, 0);
          std::vector<uint32_t> floodStack;

          // Seed texels: those inside a cell the coarse flood fill assigned to
          // this basin.
          std::vector<uint32_t> seedTexels;
          for (uint32_t tz = 0; tz < NZ; ++tz) {
            uint32_t gz =
                nearestGrid(minZ + tz * surfaceCell, createInfo.worldCenter.y,
                            halfSize.y, createInfo.worldSize.y, gridZ);
            for (uint32_t tx = 0; tx < NX; ++tx) {
              uint32_t gx =
                  nearestGrid(minX + tx * surfaceCell, createInfo.worldCenter.x,
                              halfSize.x, createInfo.worldSize.x, gridX);
              if (inBasin[static_cast<size_t>(gz) * gridX + gx])
                seedTexels.push_back(tz * NX + tx);
            }
          }

          // Flood the fine lattice at `level`: seeds below the level grow
          // through every connected texel below it (so narrow channels the
          // coarse grid missed are included; pockets behind a ridge are never
          // reached). Returns true if the water reached the lattice border -
          // physically that means it is running out of the basin (the coarse
          // grid's spill level was higher than the true pour point), or the
          // lattice is too small to hold the lake.
          // Include the terrain triangle's diagonal, but not the other one.
          static const int32_t ndx[6] = {-1, 1, 0, 0, -1, 1};
          static const int32_t ndz[6] = {0, 0, -1, 1, 1, -1};
          auto runFlood = [&](float level) {
            std::fill(wet.begin(), wet.end(), uint8_t{0});
            floodStack.clear();
            for (uint32_t sIdx : seedTexels)
              if (heightTex(sIdx % NX, sIdx / NX) <= level) {
                wet[sIdx] = 1;
                floodStack.push_back(sIdx);
              }
            bool leaked = false;
            while (!floodStack.empty()) {
              uint32_t idx = floodStack.back();
              floodStack.pop_back();
              int32_t cx = static_cast<int32_t>(idx % NX);
              int32_t cz = static_cast<int32_t>(idx / NX);
              if (cx == 0 || cz == 0 || cx == static_cast<int32_t>(NX) - 1 ||
                  cz == static_cast<int32_t>(NZ) - 1)
                leaked = true;
              for (int n = 0; n < 6; ++n) {
                int32_t nx = cx + ndx[n];
                int32_t nz = cz + ndz[n];
                if (nx < 0 || nz < 0 || nx >= static_cast<int32_t>(NX) ||
                    nz >= static_cast<int32_t>(NZ))
                  continue;
                size_t nIdx = static_cast<size_t>(nz) * NX + nx;
                if (wet[nIdx])
                  continue;
                if (heightTex(nx, nz) <= level) {
                  wet[nIdx] = 1;
                  floodStack.push_back(static_cast<uint32_t>(nIdx));
                }
              }
            }
            return leaked;
          };

          if (runFlood(threshold)) {
            // Leak: settle the lake at the highest level that stays inside the
            // lattice (bisection; leaking is monotone in the level, heights
            // are cached so each pass is cheap). That is the true fine-grid
            // spill level, so water no longer hovers over the outlet or ends
            // in a straight cut at the lattice border.
            float lo = std::numeric_limits<float>::max();
            for (uint32_t sIdx : seedTexels)
              lo = std::min(lo, heightTex(sIdx % NX, sIdx / NX));
            float hi = threshold;
            if (lo < hi) {
              for (int it = 0; it < 14; ++it) {
                const float mid = 0.5f * (lo + hi);
                if (runFlood(mid))
                  hi = mid;
                else
                  lo = mid;
              }
              threshold = lo;
            }
            runFlood(threshold);
            waterY = threshold - kShoreSlack;
            lake.worldCenter.y = waterY;
#ifdef VKH_MOUNTAIN_DEBUG_PRINT
            std::fprintf(stderr,
                         "[mountain] lake at (%.0f, %.0f): fine flood leaked, "
                         "level lowered to %.2f\n",
                         lake.worldCenter.x, lake.worldCenter.z, waterY);
#endif
          }

          if (runFlood(threshold) ||
              std::none_of(seedTexels.begin(), seedTexels.end(),
                           [&](uint32_t i) {
                             return heightTex(i % NX, i / NX) < waterY;
                           })) {
            for (uint32_t memberIdx : members)
              inBasin[memberIdx] = 0;
            continue;
          }

          // Include complete terrain-aligned quads at the shore. Every outer
          // edge has dry endpoints above waterY + slack, so its whole length
          // is buried beneath the matching terrain triangle. No contour
          // snapping or extra erosion evaluations are needed.
          const float sx = surfaceCell;
          const float sz = surfaceCell;
          lake.meshCellSize = 0.5f * (sx + sz);
          auto latticeXZ = [&](int32_t x, int32_t z) {
            return glm::vec2(minX + static_cast<float>(x) * sx,
                             minZ + static_cast<float>(z) * sz);
          };
          auto inGrid = [&](int32_t x, int32_t z) {
            return x >= 0 && z >= 0 && x < static_cast<int32_t>(NX) &&
                   z < static_cast<int32_t>(NZ);
          };
          auto isWet = [&](int32_t x, int32_t z) {
            return inGrid(x, z) && wet[static_cast<size_t>(z) * NX + x];
          };

          std::vector<int32_t> vid(static_cast<size_t>(NX) * NZ, -1);
          auto vertex = [&](int32_t x, int32_t z) -> uint32_t {
            int32_t &id = vid[static_cast<size_t>(z) * NX + x];
            if (id >= 0)
              return static_cast<uint32_t>(id);
            id = static_cast<int32_t>(lake.meshXZ.size());
            glm::vec2 p = latticeXZ(x, z);
            float depth = 0.f;
            if (isWet(x, z)) {
              depth = std::max(waterY - heightTex(x, z), 0.f);
            }
            lake.meshXZ.push_back(p);
            lake.meshDepth.push_back(depth);
            return static_cast<uint32_t>(id);
          };

          for (int32_t tz = 0; tz + 1 < static_cast<int32_t>(NZ); ++tz)
            for (int32_t tx = 0; tx + 1 < static_cast<int32_t>(NX); ++tx) {
              if (!(isWet(tx, tz) || isWet(tx + 1, tz) || isWet(tx, tz + 1) ||
                    isWet(tx + 1, tz + 1)))
                continue;
              const uint32_t v0 = vertex(tx, tz);
              const uint32_t v1 = vertex(tx + 1, tz);
              const uint32_t v2 = vertex(tx + 1, tz + 1);
              const uint32_t v3 = vertex(tx, tz + 1);
              lake.meshIndices.insert(lake.meshIndices.end(), {v0, v1, v2, v3});
            }
          lake.maskResolution = glm::uvec2(NX - 1, NZ - 1);
          lake.cellMask.assign(static_cast<size_t>(NX - 1) * (NZ - 1), false);
          for (uint32_t z = 0; z + 1 < NZ; ++z)
            for (uint32_t x = 0; x + 1 < NX; ++x)
              lake.cellMask[static_cast<size_t>(z) * (NX - 1) + x] =
                  isWet(x, z) || isWet(x + 1, z) || isWet(x, z + 1) ||
                  isWet(x + 1, z + 1);
        }

        for (uint32_t memberIdx : members)
          inBasin[memberIdx] = 0;

        outLakes->push_back(lake);
      }
    }

    // std::println("[mountain] lake basins: {} connected depressions found ->
    // {} "
    //              "accepted, {} rejected (tooSmall={} tooShallow={})",
    //              componentsFound, accepted,
    //              rejectedTooSmall + rejectedTooShallow, rejectedTooSmall,
    //              rejectedTooShallow);
  }
}

std::vector<Lake> findLakesInTile(const MountainCreateInfo &createInfo,
                                  int32_t tx, int32_t tz) {
  // Half resolution: 4x fewer height evaluations, plenty for finding basins.
  const uint32_t gridX = glm::max(createInfo.resolution.x / 2u, 2u);
  const uint32_t gridZ = glm::max(createInfo.resolution.y / 2u, 2u);
  MountainCreateInfo ci = createInfo;
  ci.lakeMinCells = std::max(1u, createInfo.lakeMinCells / 4u);
  ci.worldCenter = createInfo.worldCenter +
                   glm::vec2(static_cast<float>(tx), static_cast<float>(tz)) *
                       createInfo.worldSize;
  const glm::vec2 half = ci.worldSize * 0.5f;
  std::vector<float> h(static_cast<size_t>(gridX) * gridZ);
  for (uint32_t iz = 0; iz < gridZ; ++iz)
    for (uint32_t ix = 0; ix < gridX; ++ix) {
      glm::vec2 p =
          ci.worldCenter +
          glm::vec2(glm::mix(-half.x, half.x, float(ix) / float(gridX - 1)),
                    glm::mix(-half.y, half.y, float(iz) / float(gridZ - 1)));
      h[static_cast<size_t>(iz) * gridX + ix] = evalHeight(p, ci);
    }
  std::vector<Lake> out;
  detectLakes(ci, h, gridX, gridZ, &out);
  return out;
}

std::shared_ptr<Scene<EntitySys::Vertex>>
generate(EngineContext &context, vk::DescriptorSetLayout texturesSetLayout,
         const MountainCreateInfo &createInfo, MountainHeightmap *outHeightmap,
         std::vector<Lake> *outLakes, std::vector<TreePlacement> *outTrees) {
  using Vertex = EntitySys::Vertex;

  const uint32_t gridX = glm::max(createInfo.resolution.x, 2u);
  const uint32_t gridZ = glm::max(createInfo.resolution.y, 2u);

  std::vector<Vertex> vertices;
  if (createInfo.buildMesh)
    vertices.reserve(static_cast<size_t>(gridX) * gridZ);

  glm::vec3 boundsMin(std::numeric_limits<float>::max());
  glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

  std::vector<float> heights(static_cast<size_t>(gridX) * gridZ);
  // Populated in lockstep with `heights` in the vertex loop below, reused
  // afterward for lake-basin flood filling (see the outLakes block).
  std::vector<float> slopeNorms(static_cast<size_t>(gridX) * gridZ);

  const glm::vec2 halfSize = createInfo.worldSize * 0.5f;
  const float heightRange = createInfo.peakHeight - createInfo.baseHeight;

  auto worldPosAt = [&](uint32_t ix, uint32_t iz) {
    float u = static_cast<float>(ix) / static_cast<float>(gridX - 1);
    float v = static_cast<float>(iz) / static_cast<float>(gridZ - 1);
    float x = glm::mix(-halfSize.x, halfSize.x, u);
    float z = glm::mix(-halfSize.y, halfSize.y, v);
    return glm::vec2(x, z);
  };

  // Only the initial finite heightmap is parallelized here; streamed lake
  // tiles already run on workers. Cap concurrency to leave CPU headroom.
  const uint32_t hardware = std::thread::hardware_concurrency();
  const uint32_t workers =
      std::min(gridZ, std::min(8u, hardware > 1 ? hardware - 1 : 1u));
  auto heightRows = [&](uint32_t worker) {
    for (uint32_t iz = worker; iz < gridZ; iz += workers)
      for (uint32_t ix = 0; ix < gridX; ++ix)
        heights[static_cast<size_t>(iz) * gridX + ix] =
            evalHeight(worldPosAt(ix, iz), createInfo);
  };
  std::vector<std::future<void>> heightJobs;
  for (uint32_t worker = 1; worker < workers; ++worker)
    heightJobs.push_back(std::async(std::launch::async, heightRows, worker));
  heightRows(0);
  for (auto &job : heightJobs)
    job.get();

  // Cell size, used to compute normals from central differences on the
  // already-evaluated height grid rather than re-evaluating the (fairly
  // expensive) erosion function again.
  float cellX = createInfo.worldSize.x / static_cast<float>(gridX - 1);
  float cellZ = createInfo.worldSize.y / static_cast<float>(gridZ - 1);

  // If requested, accumulated in lockstep with the vertex loop below so
  // the exported heightmap always matches the mesh's own hNorm/slopeNorm
  // exactly - no separate re-evaluation, no risk of drifting out of sync.
  //
  // Stored as packed half-floats (glm::packHalf2x16), one uint32 per
  // texel, matching an R16G16_SFLOAT image - NOT plain 32-bit floats.
  // R32G32_SFLOAT is only required by the Vulkan spec to support nearest
  // filtering; linear filtering on 32-bit float formats is optional and
  // frequently unsupported, while 16-bit float formats are part of the
  // mandatory linear-filtering set. Sampling with an unsupported filter
  // mode is undefined behavior - inconsistent-looking results that don't
  // match the actual terrain (right shape, wrong place; or a hard seam
  // partway across an otherwise-uniform area) are exactly what that looks
  // like in practice. Half-float precision is more than enough here: the
  // height value maxes out around peakHeight (tens of world units) and
  // the suitability channel is already clamped to [0,1].
  std::vector<uint32_t> heightmapPixels;
  if (outHeightmap)
    heightmapPixels.resize(static_cast<size_t>(gridX) * gridZ);

  for (uint32_t iz = 0; iz < gridZ; ++iz) {
    for (uint32_t ix = 0; ix < gridX; ++ix) {
      glm::vec2 xz = worldPosAt(ix, iz);
      float hNorm = heights[iz * gridX + ix];
      float y = createInfo.baseHeight + hNorm * heightRange;

      uint32_t ixL = ix > 0 ? ix - 1 : ix;
      uint32_t ixR = ix < gridX - 1 ? ix + 1 : ix;
      uint32_t izD = iz > 0 ? iz - 1 : iz;
      uint32_t izU = iz < gridZ - 1 ? iz + 1 : iz;

      float hL = heights[iz * gridX + ixL] * heightRange;
      float hR = heights[iz * gridX + ixR] * heightRange;
      float hD = heights[izD * gridX + ix] * heightRange;
      float hU = heights[izU * gridX + ix] * heightRange;

      float dHdx = (hR - hL) / (glm::max(1u, ixR - ixL) * cellX);
      float dHdz = (hU - hD) / (glm::max(1u, izU - izD) * cellZ);

      glm::vec3 normal = glm::normalize(glm::vec3(-dHdx, 1.f, -dHdz));

      // Slope steepness, normalized so 0 = flat and 1 = at-or-beyond
      // rockSlopeAngleDeg from horizontal. normal.y == cos(angle from
      // vertical-up), so a flat surface has normal.y == 1 and a surface
      // at the threshold angle has normal.y == cos(rockSlopeAngleDeg).
      float thresholdNormalY =
          std::cos(glm::radians(createInfo.rockSlopeAngleDeg));
      float slopeNorm = 0.f;
      if (thresholdNormalY < 1.f - 1e-5f) {
        slopeNorm =
            glm::clamp((1.f - normal.y) / (1.f - thresholdNormalY), 0.f, 1.f);
      }

      slopeNorms[iz * gridX + ix] = slopeNorm;

      Vertex vert{};
      vert.pos = glm::vec3(xz.x, y, xz.y);
      vert.normal = normal;
      vert.uv = glm::vec2(glm::clamp(hNorm, 0.f, 1.f), slopeNorm);
      if (createInfo.buildMesh)
        vertices.push_back(vert);

      if (outHeightmap) {
        size_t pIdx = static_cast<size_t>(iz) * gridX + ix;
        float suitability =
            computeGrassSuitability(glm::clamp(hNorm, 0.f, 1.f), slopeNorm);
        heightmapPixels[pIdx] = glm::packHalf2x16(glm::vec2(y, suitability));
      }

      boundsMin = glm::min(boundsMin, vert.pos);
      boundsMax = glm::max(boundsMax, vert.pos);
    }
  }

  if (outLakes && createInfo.generateLakes)
    detectLakes(createInfo, heights, gridX, gridZ, outLakes);

  if (outTrees && createInfo.generateTrees) {
    outTrees->clear();
    // Separate RNG stream from the height/erosion noise (different salt on
    // the same seed) so tree placement doesn't visibly lock to the phase of
    // the terrain noise - e.g. trees clustering in a stripe pattern that
    // matches the erosion gullies would look artificial.
    std::mt19937 treeRng(createInfo.seed ^ 0x7ee5eedu);
    std::uniform_real_distribution<float> jitter01(0.f, 1.f);
    std::uniform_real_distribution<float> yawDist(0.f, glm::two_pi<float>());
    std::uniform_real_distribution<float> scaleDist(0.8f, 1.3f);
    std::uniform_int_distribution<uint32_t> variantDist(
        0, glm::max(1u, createInfo.treeVariantCount) - 1);

    const float cellSize = glm::max(createInfo.treeMinSpacing, 0.1f);
    const int cellsX =
        static_cast<int>(std::ceil(createInfo.worldSize.x / cellSize));
    const int cellsZ =
        static_cast<int>(std::ceil(createInfo.worldSize.y / cellSize));

    // Nearest-neighbor sample into the already-evaluated heights/slopeNorms
    // grid - reuses the exact same data the heightmap/grass-suitability
    // texture above are built from, no re-evaluation of evalHeight().
    auto sampleGrid = [&](glm::vec2 worldXZ, float &hNormOut,
                          float &slopeNormOut) {
      float u = (worldXZ.x + halfSize.x) / createInfo.worldSize.x;
      float v = (worldXZ.y + halfSize.y) / createInfo.worldSize.y;
      int ix = glm::clamp(static_cast<int>(u * (gridX - 1) + 0.5f), 0,
                          static_cast<int>(gridX) - 1);
      int iz = glm::clamp(static_cast<int>(v * (gridZ - 1) + 0.5f), 0,
                          static_cast<int>(gridZ) - 1);
      size_t idx = static_cast<size_t>(iz) * gridX + ix;
      hNormOut = heights[idx];
      slopeNormOut = slopeNorms[idx];
    };

    // Point-in-lake test using the same bounding-box + rasterized cellMask
    // footprint WaterSys/generateLakeMesh use, so trees never spawn
    // standing in an irregularly-shaped lake even though its bounding box
    // is axis-aligned and may cover dry land too.
    auto insideLake = [&](glm::vec2 worldXZ) {
      if (!outLakes)
        return false;
      for (const Lake &lake : *outLakes) {
        glm::vec2 half = lake.worldSize * 0.5f;
        glm::vec2 c(lake.worldCenter.x, lake.worldCenter.z);
        glm::vec2 rel = worldXZ - (c - half);
        if (rel.x < 0.f || rel.y < 0.f || rel.x > lake.worldSize.x ||
            rel.y > lake.worldSize.y)
          continue;
        if (lake.maskResolution.x == 0 || lake.maskResolution.y == 0)
          return true; // no mask - fall back to bounding box only
        uint32_t mx =
            glm::clamp(static_cast<uint32_t>(rel.x / lake.worldSize.x *
                                             lake.maskResolution.x),
                       0u, lake.maskResolution.x - 1);
        uint32_t mz =
            glm::clamp(static_cast<uint32_t>(rel.y / lake.worldSize.y *
                                             lake.maskResolution.y),
                       0u, lake.maskResolution.y - 1);
        if (lake.cellMask[mz * lake.maskResolution.x + mx])
          return true;
      }
      return false;
    };

    for (int cz = 0; cz < cellsZ; ++cz) {
      for (int cx = 0; cx < cellsX; ++cx) {
        // One jittered candidate per cell (jittered-grid dart-throwing).
        float jx = (static_cast<float>(cx) + jitter01(treeRng)) * cellSize;
        float jz = (static_cast<float>(cz) + jitter01(treeRng)) * cellSize;
        glm::vec2 worldXZ(-halfSize.x + jx, -halfSize.y + jz);
        if (worldXZ.x > halfSize.x || worldXZ.y > halfSize.y)
          continue;

        float hNorm, slopeNorm;
        sampleGrid(worldXZ, hNorm, slopeNorm);

        if (hNorm < createInfo.treeMinAltitude ||
            hNorm > createInfo.treeMaxAltitude)
          continue;
        if (slopeNorm > createInfo.treeMaxSlopeNorm)
          continue;

        // Smooth fade near the altitude/slope limits instead of a hard
        // cutoff - reads as "forest thins into open rock/treeline" rather
        // than a fenced-off patch of trees stopping dead at a line.
        float altitudeFade =
            glm::smoothstep(createInfo.treeMaxAltitude,
                            createInfo.treeMaxAltitude * 0.7f, hNorm) *
            glm::smoothstep(createInfo.treeMinAltitude,
                            createInfo.treeMinAltitude * 1.5f + 0.02f, hNorm);
        float slopeFade =
            1.f - glm::smoothstep(createInfo.treeMaxSlopeNorm * 0.5f,
                                  createInfo.treeMaxSlopeNorm, slopeNorm);
        float acceptChance = createInfo.treeDensity * altitudeFade * slopeFade;

        if (jitter01(treeRng) > acceptChance)
          continue;
        if (insideLake(worldXZ))
          continue;

        float y = createInfo.baseHeight + hNorm * heightRange;
        TreePlacement tree{};
        tree.position = glm::vec3(worldXZ.x, y, worldXZ.y);
        tree.yaw = yawDist(treeRng);
        tree.scale = scaleDist(treeRng);
        tree.variant = variantDist(treeRng);
        outTrees->push_back(tree);
      }
    }

    // std::println("[mountain] scattered %{} trees", outTrees->size());
  }

  std::shared_ptr<Scene<Vertex>> scene;
  if (createInfo.buildMesh) {
    std::vector<uint32_t> indices;
    indices.reserve(static_cast<size_t>(gridX - 1) * (gridZ - 1) * 6);
    for (uint32_t iz = 0; iz < gridZ - 1; ++iz) {
      for (uint32_t ix = 0; ix < gridX - 1; ++ix) {
        uint32_t i0 = iz * gridX + ix;
        uint32_t i1 = i0 + 1;
        uint32_t i2 = i0 + gridX;
        uint32_t i3 = i2 + 1;

        indices.push_back(i0);
        indices.push_back(i2);
        indices.push_back(i1);

        indices.push_back(i1);
        indices.push_back(i2);
        indices.push_back(i3);
      }
    }

    glm::uvec2 texSize{};
    std::vector<uint8_t> texPixels = buildTerrainTexture(texSize);

    SceneCreateInfo<Vertex> sceneInfo{vertices, indices};
    sceneInfo.texturePixels = std::move(texPixels);
    sceneInfo.textureSize = texSize;

    scene =
        std::make_shared<Scene<Vertex>>(context, sceneInfo, texturesSetLayout);

    // The raw-vertex constructor default-constructs Mesh{} (aabb{} is
    // degenerate/zero-sized), so without this the compute-based frustum
    // culling pass will treat the whole mountain as invisible.
    for (auto &mesh : scene->meshes) {
      mesh.aabb.min = boundsMin;
      mesh.aabb.max = boundsMax;
    }
  }

  if (outHeightmap) {
    ImageCreateInfo_data imageInfo{};
    imageInfo.size = glm::uvec2(gridX, gridZ);
    imageInfo.data = heightmapPixels.data();
    imageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
    imageInfo.usage =
        vk::ImageUsageFlagBits::eTransferDst | vk::ImageUsageFlagBits::eSampled;
    // R16G16_SFLOAT, not R32G32_SFLOAT: see the comment on heightmapPixels
    // above - 32-bit float formats aren't guaranteed to support linear
    // filtering in Vulkan, 16-bit ones are, and precision is more than
    // sufficient for a height value + a [0,1] mask here.
    imageInfo.format = vk::Format::eR16G16Sfloat;
    imageInfo.name = "mountain heightmap image";

    outHeightmap->image = std::make_shared<Image2D>(context, imageInfo);
    outHeightmap->worldMin = -halfSize;
    outHeightmap->worldSize = createInfo.worldSize;
  }

  return scene;
}

std::shared_ptr<Scene<EntitySys::Vertex>>
generateLakeMesh(EngineContext &context,
                 vk::DescriptorSetLayout texturesSetLayout, const Lake &lake) {
  using Vertex = EntitySys::Vertex;

  // A single flat quad sized to the basin's bounding box. Basins found by
  // the flood fill are already axis-aligned blobs of grid cells, so an
  // axis-aligned quad covers them without needing to match their (often
  // irregular) outline exactly - a small amount of water clipping into the
  // surrounding shore is preferable to leaving gaps at the edges.
  const glm::vec2 half = lake.worldSize * 0.5f;
  const float y = lake.worldCenter.y;
  const float cx = lake.worldCenter.x;
  const float cz = lake.worldCenter.z;

  // Same corner layout/winding as the main terrain grid's quads (i0=(ix,iz),
  // i1=(ix+1,iz), i2=(ix,iz+1), i3=(ix+1,iz+1), triangulated i0,i2,i1 then
  // i1,i2,i3) so the lake's front face and normal match the terrain mesh's
  // convention.
  std::vector<Vertex> vertices(4);
  vertices[0].pos = glm::vec3(cx - half.x, y, cz - half.y); // i0
  vertices[1].pos = glm::vec3(cx + half.x, y, cz - half.y); // i1
  vertices[2].pos = glm::vec3(cx - half.x, y, cz + half.y); // i2
  vertices[3].pos = glm::vec3(cx + half.x, y, cz + half.y); // i3
  for (auto &v : vertices) {
    v.normal = glm::vec3(0.f, 1.f, 0.f);
  }
  vertices[0].uv = glm::vec2(0.f, 0.f);
  vertices[1].uv = glm::vec2(1.f, 0.f);
  vertices[2].uv = glm::vec2(0.f, 1.f);
  vertices[3].uv = glm::vec2(1.f, 1.f);

  std::vector<uint32_t> indices{0, 2, 1, 1, 2, 3};

  // A tiny solid-color texture: a muted, slightly reflective blue - this is
  // a static stand-in surface, not the animated Tessendorf ocean WaterSys
  // renders, so no wave/foam detail, just a plausible lake-water tint.
  glm::vec3 linearColor(0.05f, 0.14f, 0.22f);
  glm::vec3 encoded =
      glm::pow(glm::clamp(linearColor, 0.f, 1.f), glm::vec3(1.f / 2.2f));
  std::vector<uint8_t> texPixels = {
      static_cast<uint8_t>(encoded.r * 255.0f + 0.5f),
      static_cast<uint8_t>(encoded.g * 255.0f + 0.5f),
      static_cast<uint8_t>(encoded.b * 255.0f + 0.5f),
      120, // reflectivity/alpha - see buildTerrainTexture's Stop comment
  };

  SceneCreateInfo<Vertex> sceneInfo{vertices, indices};
  sceneInfo.texturePixels = std::move(texPixels);
  sceneInfo.textureSize = glm::uvec2(1, 1);

  auto scene =
      std::make_shared<Scene<Vertex>>(context, sceneInfo, texturesSetLayout);

  for (auto &mesh : scene->meshes) {
    mesh.aabb.min = glm::vec3(cx - half.x, y, cz - half.y);
    mesh.aabb.max = glm::vec3(cx + half.x, y, cz + half.y);
  }

  return scene;
}

std::function<float(glm::vec2, float)>
makeHeightFn(const MountainCreateInfo &createInfo) {
  const float heightRange = createInfo.peakHeight - createInfo.baseHeight;
  return [createInfo, heightRange](glm::vec2 xz, float cellSize) {
    return createInfo.baseHeight +
           evalHeight(xz, createInfo, cellSize) * heightRange;
  };
}

std::function<float(glm::vec2)>
makeCoarseHeightFn(const MountainCreateInfo &createInfo, float cellSize) {
  const float heightRange = createInfo.peakHeight - createInfo.baseHeight;
  return [createInfo, heightRange, cellSize](glm::vec2 xz) {
    return createInfo.baseHeight +
           evalHeight(xz, createInfo, cellSize) * heightRange;
  };
}

} // namespace vkh::mountain
