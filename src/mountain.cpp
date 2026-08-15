#include "mountain.hpp"

#include "vkh/sceneBuilder.hpp"

#include <glm/gtc/constants.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
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
  return glm::vec2(s.x - std::floor(s.x), s.y - std::floor(s.y)) * 2.0f -
        1.0f;
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

struct StripeSample {
  float height{0.f};       // normalized gully height, roughly [-1, 1]
  glm::vec2 trueGrad{0.f}; // real (non-faked) gradient of the stripe
  glm::vec2 straightGrad{0.f}; // "straight gullies" faked gradient, using
                               // sign(sin) instead of sin, so child octaves
                               // branch off cleanly rather than curling -
                               // see "Straight gullies" in the post
};

// One octave of directional stripe noise, blended across the 3x3
// neighborhood of cell pivots (see "Generating stripes"). Implements
// "Normalized gullies": the cos/sin pair from each cell is treated as a
// point on a unit circle, blended, then re-normalized to a consistent
// magnitude using the scale-by-k-then-clamp trick (k=2, so anything with
// blended length > 0.5 becomes fully normalized) to avoid the loopy
// artifacts full normalization produces.
StripeSample sampleStripe(glm::vec2 pos, glm::vec2 dir, float freq,
                          uint32_t seed) {
  glm::vec2 tangent(-dir.y, dir.x);
  glm::vec2 cellPos = pos * freq;
  glm::vec2 cellId = glm::floor(cellPos);

  float cosSum = 0.f, sinSum = 0.f, wSum = 0.f;

  for (int dy = -1; dy <= 1; ++dy) {
    for (int dx = -1; dx <= 1; ++dx) {
      glm::vec2 cell = cellId + glm::vec2(float(dx), float(dy));
      glm::vec2 pivotJitter = hash2(cell, seed) * 0.5f + 0.5f;
      glm::vec2 pivot = cell + pivotJitter;

      glm::vec2 toP = cellPos - pivot;
      float dist = glm::length(toP);
      float w = glm::clamp(1.5f - dist, 0.f, 1.f);
      if (w <= 0.f)
        continue;
      w = w * w * (3.f - 2.f * w); // smoothstep falloff

      float phase = glm::dot(toP, tangent) * glm::pi<float>();
      cosSum += std::cos(phase) * w;
      sinSum += std::sin(phase) * w;
      wSum += w;
    }
  }

  glm::vec2 wave(0.f, 0.f);
  if (wSum > 1e-5f)
    wave = glm::vec2(cosSum, sinSum) / wSum;

  // Normalized gullies: scale by k=2 then clamp to the unit circle, so
  // blended lengths above 0.5 become fully normalized, avoiding both
  // inconsistent magnitude (no normalization) and loopy artifacts (full
  // normalization).
  glm::vec2 scaled = wave * 2.f;
  float len = glm::length(scaled);
  if (len > 1.f)
    scaled /= len;

  StripeSample result;
  result.height = scaled.x;
  float trueSlope = scaled.y;
  result.trueGrad = tangent * (trueSlope * glm::pi<float>() * freq);

  // Straight gullies: use the sign of the sine wave rather than its value,
  // as if the gully were an extruded triangle wave with constant slope
  // top-to-bottom, so children branch off cleanly.
  float straightSlope = (scaled.y > 0.f) ? 1.f : (scaled.y < 0.f ? -1.f : 0.f);
  result.straightGrad = tangent * (straightSlope * glm::pi<float>() * freq);

  return result;
}

// Evaluates the eroded height at a world-space XZ position, following the
// post's "fade approach" + "stacked fading": each octave's gully is
// blended against a running fade-target/mask via mix(), replacing the
// running result rather than summing octaves, so that finer octaves can't
// disturb the ridges/creases already committed by coarser ones.
float evalHeight(glm::vec2 pos, const MountainCreateInfo &info) {
  // Smooth base landscape: a couple of broad low-frequency bumps so
  // there's an actual peak for the erosion to carve into.
  float baseFreq = 1.2f / glm::max(info.worldSize.x, info.worldSize.y);
  auto sampleBase = [&](glm::vec2 samplePos) {
    glm::vec2 pp = samplePos * baseFreq;
    float h = 0.6f * std::cos(pp.x * glm::two_pi<float>()) *
                  std::cos(pp.y * glm::two_pi<float>()) +
              0.3f * std::cos((pp.x + pp.y) * glm::two_pi<float>() * 1.7f +
                              1.3f);
    return h * 0.5f + 0.5f;
  };
  float baseH = sampleBase(pos);

  float eps = 0.5f / baseFreq * 0.01f;
  float baseGradX =
      (sampleBase(pos + glm::vec2(eps, 0.f)) -
       sampleBase(pos - glm::vec2(eps, 0.f))) /
      (2.f * eps);
  float baseGradY =
      (sampleBase(pos + glm::vec2(0.f, eps)) -
       sampleBase(pos - glm::vec2(0.f, eps))) /
      (2.f * eps);
  glm::vec2 baseGrad(baseGradX, baseGradY);

  // Input fade target: -1 at valleys, +1 at peaks (inverse_lerp from the
  // "The fade approach" section).
  float fadeTarget = inverseLerp(0.f, 1.f, baseH) * 2.f - 1.f;

  // Input mask: opaque (1, "let erosion through") on slopes, closed
  // (0, "protect the base shape") at flat peaks/valleys of the base
  // landscape, via the same ease_out shaping used for stacked fading.
  float baseSlopeNorm =
      glm::clamp(glm::length(baseGrad) * info.slopeSensitivity, 0.f, 1.f);
  float mask = easeOut(baseSlopeNorm);

  // Direction/magnitude driving the *next* octave's stripe orientation -
  // uses the unfaded "straight gullies" gradient, per "Straight gullies".
  glm::vec2 gullyDirAccum = baseGrad;

  float freq = info.baseFrequency;
  float amplitude = 1.f;

  for (uint32_t o = 0; o < info.octaves; ++o) {
    float slopeMag = glm::length(gullyDirAccum);
    glm::vec2 dir =
        slopeMag > 1e-5f ? gullyDirAccum / slopeMag : glm::vec2(1.f, 0.f);

    // The frequency approach: stripes get thicker as slope -> 0, so peaks
    // and valleys always land on a stripe center (ridge) rather than an
    // arbitrary point. Clamped to minFrequencyScale instead of true
    // infinity to avoid numerical blowup.
    float slopeNorm =
        glm::clamp(slopeMag * info.slopeSensitivity, 0.f, 1.f);
    float freqScale = glm::mix(info.minFrequencyScale, 1.f, slopeNorm);
    float effectiveFreq = freq * freqScale;

    StripeSample sample =
        sampleStripe(pos, dir, effectiveFreq, info.seed + o * 977u);

    // Pointy peaks: scale down the gully contribution (erosionStrength is
    // expected to compensate by being ~1/gullyWeight).
    float octH = sample.height * amplitude * info.gullyWeight *
                info.erosionStrength;
    glm::vec2 octStraightGrad = sample.straightGrad * amplitude *
                                info.gullyWeight * info.erosionStrength;

    // Fade approach: this octave's result *replaces* the running fade
    // target wherever the mask permits, rather than adding to it.
    fadeTarget = glm::mix(fadeTarget, octH, mask);

    // Stacked fading: build this octave's mask contribution, opaque (0)
    // at this octave's own creases/ridges (where its stripe is flat, i.e.
    // |trueGrad| ~ 0) so finer octaves can't disturb them, permissive (1)
    // on its slopes so finer octaves can still add detail there.
    float localSlopeNorm = glm::clamp(glm::abs(sample.height), 0.f, 1.f);
    // sample.height ~ +-1 exactly where the stripe is flat (ridge/crease),
    // ~0 where it's steepest - so 1 - |height| tracks local slope.
    float newMaskContribution = easeOut(1.f - localSlopeNorm);
    mask = powInv(mask, info.detail) * newMaskContribution;

    // Feed the unfaded, straight-gullies gradient forward to orient the
    // next (finer) octave.
    gullyDirAccum += octStraightGrad;

    amplitude *= info.gain;
    freq *= info.lacunarity;
  }

  return glm::clamp(fadeTarget * 0.5f + 0.5f, 0.f, 1.f);
}

// Builds a small altitude gradient strip: rock (low/steep) -> dirt ->
// grass -> snow (high), 5 stops, as a 5x1 RGBA8 image. Vertex UV.x encodes
// normalized altitude so the fragment shader's texture sample effectively
// looks the color up by height.
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
std::vector<uint8_t> buildGradientTexture(glm::uvec2 &outSize) {
  struct Stop {
    glm::vec3 linearColor; // 0..1, linear space
  };
  // Ordered low -> high altitude.
  static constexpr std::array<Stop, 5> stops{{
      {{0.14f, 0.10f, 0.07f}}, // dark rock / scree at the base
      {{0.20f, 0.13f, 0.06f}}, // dirt
      {{0.08f, 0.20f, 0.05f}}, // grass
      {{0.30f, 0.30f, 0.30f}}, // bare rock near the peak
      {{0.90f, 0.92f, 0.95f}}, // snow cap
  }};
  outSize = glm::uvec2(static_cast<uint32_t>(stops.size()), 1u);
  std::vector<uint8_t> pixels(stops.size() * 4);
  for (size_t i = 0; i < stops.size(); ++i) {
    glm::vec3 encoded = glm::pow(glm::clamp(stops[i].linearColor, 0.f, 1.f),
                                 glm::vec3(1.f / 2.2f));
    pixels[i * 4 + 0] = static_cast<uint8_t>(encoded.r * 255.0f + 0.5f);
    pixels[i * 4 + 1] = static_cast<uint8_t>(encoded.g * 255.0f + 0.5f);
    pixels[i * 4 + 2] = static_cast<uint8_t>(encoded.b * 255.0f + 0.5f);
    pixels[i * 4 + 3] = 255;
  }
  return pixels;
}

} // namespace

std::shared_ptr<Scene<EntitySys::Vertex>>
generate(EngineContext &context, vk::DescriptorSetLayout texturesSetLayout,
        const MountainCreateInfo &createInfo) {
  using Vertex = EntitySys::Vertex;

  const uint32_t gridX = glm::max(createInfo.resolution.x, 2u);
  const uint32_t gridZ = glm::max(createInfo.resolution.y, 2u);

  std::vector<Vertex> vertices;
  vertices.reserve(static_cast<size_t>(gridX) * gridZ);

  glm::vec3 boundsMin(std::numeric_limits<float>::max());
  glm::vec3 boundsMax(std::numeric_limits<float>::lowest());

  std::vector<float> heights(static_cast<size_t>(gridX) * gridZ);

  const glm::vec2 halfSize = createInfo.worldSize * 0.5f;
  const float heightRange = createInfo.peakHeight - createInfo.baseHeight;

  auto worldPosAt = [&](uint32_t ix, uint32_t iz) {
    float u = static_cast<float>(ix) / static_cast<float>(gridX - 1);
    float v = static_cast<float>(iz) / static_cast<float>(gridZ - 1);
    float x = glm::mix(-halfSize.x, halfSize.x, u);
    float z = glm::mix(-halfSize.y, halfSize.y, v);
    return glm::vec2(x, z);
  };

  for (uint32_t iz = 0; iz < gridZ; ++iz) {
    for (uint32_t ix = 0; ix < gridX; ++ix) {
      glm::vec2 xz = worldPosAt(ix, iz);
      float h = evalHeight(xz, createInfo);
      heights[iz * gridX + ix] = h;
    }
  }

  // Cell size, used to compute normals from central differences on the
  // already-evaluated height grid rather than re-evaluating the (fairly
  // expensive) erosion function again.
  float cellX = createInfo.worldSize.x / static_cast<float>(gridX - 1);
  float cellZ = createInfo.worldSize.y / static_cast<float>(gridZ - 1);

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

      Vertex vert{};
      vert.pos = glm::vec3(xz.x, y, xz.y);
      vert.normal = normal;
      vert.uv = glm::vec2(glm::clamp(hNorm, 0.f, 1.f), 0.5f);
      vertices.push_back(vert);

      boundsMin = glm::min(boundsMin, vert.pos);
      boundsMax = glm::max(boundsMax, vert.pos);
    }
  }

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
  std::vector<uint8_t> texPixels = buildGradientTexture(texSize);

  SceneCreateInfo<Vertex> sceneInfo{vertices, indices};
  sceneInfo.texturePixels = std::move(texPixels);
  sceneInfo.textureSize = texSize;

  auto scene =
      std::make_shared<Scene<Vertex>>(context, sceneInfo, texturesSetLayout);

  // The raw-vertex constructor default-constructs Mesh{} (aabb{} is
  // degenerate/zero-sized), so without this the compute-based frustum
  // culling pass will treat the whole mountain as invisible.
  for (auto &mesh : scene->meshes) {
    mesh.aabb.min = boundsMin;
    mesh.aabb.max = boundsMax;
  }

  return scene;
}

} // namespace vkh::mountain
