#pragma once

// Procedural fir generator for TreeSys.
//
// generate() returns one vertex/index buffer pair split into two parts:
//   part 0: trunk   (meshIsFoliage = false)
//   part 1: foliage (meshIsFoliage = true)
// Both parts sample ONE shared atlas texture (makeAtlas()), so the whole
// variant needs a single material:
//   u in [0.00, 0.75): needle sprig (alpha cutout), v = 0 at branch base
//   u in [0.75, 1.00): bark strip
//
// Local space: Y up, trunk base at origin, tree height ~= Params::height.
// Use TreePlacement::scale for size differences between instances.
//
// Vertex.color: r = wind stiffness (0 trunk, ~0.1 branch root -> 1 tip),
//               g = baked AO-ish darkening (1 = open, lower near trunk/bottom;
//                   ignored by shaders that don't read it),
//               b = 0, a = 1.

#include "../../engineContext.hpp"
#include "../../scene.hpp"
#include "../../sceneBuilder.hpp"
#include "treeSys.hpp"

#include <glm/glm.hpp>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <map>
#include <memory>
#include <random>
#include <vector>

namespace vkh::fir {

struct Params {
  bool broadleaf{false};
  uint32_t seed = 1;
  float height = 9.f;
  float trunkBaseRadius = 0.28f;
  float trunkTopRadius = 0.03f;
  uint32_t trunkSides = 8;
  uint32_t trunkRings = 6;

  uint32_t tiers = 24;         // branch whorls up the tree
  float crownStart = 0.10f;    // fraction of height where foliage begins
  float maxCrownRadius = 2.3f; // branch length at the lowest tier
  uint32_t branchesBottom = 13;
  uint32_t branchesTop = 6;
  float cardWidth = 1.5f; // sprig width at the lowest tier
  uint32_t cardSegments = 4;
  float droopBottom = 0.35f; // tip drop as fraction of branch length
  float droopTop = 0.08f;
  float liftBottom = 0.05f; // upward sweep near the trunk, fraction of length
  float liftTop = 0.35f;    // upper branches sweep up more (conical crown)
  uint32_t fanCards = 3;    // cards per branch, fanned by rolling around it
  float fanRollDeg = 38.f;  // roll step between fanned cards
  float fanYawDeg = 12.f;   // yaw spread between fanned cards
};

struct Part {
  uint32_t indexOffset = 0;
  uint32_t indexCount = 0;
  bool foliage = false;
  glm::vec3 aabbMin{0.f};
  glm::vec3 aabbMax{0.f};
};

struct Mesh {
  std::vector<TreeSys::Vertex> vertices;
  std::vector<uint32_t> indices;
  std::vector<Part> parts; // [0] trunk, [1] foliage
};

namespace detail {

constexpr float kPi = 3.14159265358979f;
constexpr float kFoliageU = 0.75f; // atlas split

inline float mixf(float a, float b, float t) { return a + (b - a) * t; }

inline float hash01(uint32_t x) {
  x ^= x >> 16;
  x *= 0x7feb352dU;
  x ^= x >> 15;
  x *= 0x846ca68bU;
  x ^= x >> 16;
  return static_cast<float>(x & 0xFFFFFF) / static_cast<float>(0x1000000);
}

struct Builder {
  Mesh &m;
  const Params &p;
  std::mt19937 rng;

  Builder(Mesh &mesh, const Params &params)
      : m(mesh), p(params), rng(params.seed) {}

  float rnd(float a, float b) {
    return std::uniform_real_distribution<float>(a, b)(rng);
  }

  struct Branch {
    glm::vec3 origin, direction;
    float length, lift, droop, width, tier;
  };
  std::vector<Branch> branches;
  void planBranches() {
    const float H = p.height;
    const uint32_t T = std::max(2u, p.tiers);
    const float spacing = (.96f - p.crownStart) * H / (T - 1);
    for (uint32_t t = 0; t < T; ++t) {
      const float f = float(t) / (T - 1);
      const float h = mixf(p.crownStart, .90f, f) * H;
      const float crown =
          p.broadleaf
              ? std::sqrt(std::max(.08f, 1.f - std::pow((f - .5f) / .58f, 2.f)))
              : std::pow(1.f - f, .85f);
      const float R = p.maxCrownRadius * crown + .15f;
      const uint32_t B = uint32_t(
          std::lround(mixf(float(p.branchesBottom), float(p.branchesTop), f)));
      const float az0 = t * 2.399963f + rnd(0, .5f);
      for (uint32_t b = 0; b < B; ++b) {
        float az = az0 + 2 * kPi * b / B + rnd(-.3f, .3f);
        glm::vec3 d(std::cos(az), 0, std::sin(az));
        float y = h + rnd(-.25f, .25f) * spacing;
        float L = R * rnd(.68f, 1.17f);
        float lift = mixf(p.liftBottom, p.liftTop, f) * L;
        float droop = mixf(p.droopBottom, p.droopTop, f) * L;
        float W = p.cardWidth * mixf(1, .6f, f) * rnd(.85f, 1.15f);
        branches.push_back({d * trunkRadiusAt(y / H) + glm::vec3(0, y, 0), d, L,
                            lift, droop, W, f});
      }
    }
  }
  void buildWoodBranches() {
    const uint32_t sides = p.trunkSides >= 8 ? 5 : 3;
    const uint32_t rings = p.trunkRings >= 6 ? 3 : 2;
    for (const auto &branch : branches) {
      uint32_t start = uint32_t(m.vertices.size());
      glm::vec3 side =
          glm::normalize(glm::cross(glm::vec3(0, 1, 0), branch.direction));
      for (uint32_t r = 0; r <= rings; ++r) {
        float s = float(r) / rings;
        glm::vec3 c = branch.origin + branch.direction * (branch.length * s) +
                      glm::vec3(0, branch.lift * s - branch.droop * s * s, 0);
        glm::vec3 tangent =
            glm::normalize(branch.direction * branch.length +
                           glm::vec3(0, branch.lift - 2 * branch.droop * s, 0));
        glm::vec3 up = glm::normalize(glm::cross(tangent, side));
        float radius =
            std::max(.008f, trunkRadiusAt(branch.origin.y / p.height) * .3f *
                                std::pow(1 - s, 1.4f));
        for (uint32_t k = 0; k <= sides; ++k) {
          float a = 2 * kPi * k / sides;
          glm::vec3 n = side * std::cos(a) + up * std::sin(a);
          addVertex(c + n * radius, n, {kFoliageU + .02f + .23f * k / sides, s},
                    {.03f * branch.tier * branch.tier + .6f * s * s,
                     .7f + .3f * s, 0, 1});
        }
      }
      for (uint32_t r = 0; r < rings; ++r)
        for (uint32_t k = 0; k < sides; ++k) {
          uint32_t a = start + r * (sides + 1) + k, b = a + 1,
                   c = a + sides + 1, d = c + 1;
          addTri(a, b, c);
          addTri(b, d, c);
        }
    }
  }

  float trunkRadiusAt(float t) const {
    return mixf(p.trunkBaseRadius, p.trunkTopRadius,
                std::pow(glm::clamp(t, 0.f, 1.f), 0.8f));
  }

  uint32_t addVertex(glm::vec3 pos, glm::vec3 n, glm::vec2 uv, glm::vec4 col) {
    TreeSys::Vertex v;
    v.pos = pos;
    v.normal = glm::normalize(n);
    v.uv = uv;
    v.color = col;
    m.vertices.push_back(v);
    return static_cast<uint32_t>(m.vertices.size() - 1);
  }

  void addTri(uint32_t a, uint32_t b, uint32_t c) {
    m.indices.push_back(a);
    m.indices.push_back(b);
    m.indices.push_back(c);
  }

  void buildTrunk() {
    const float H = p.height * 0.97f;
    const float bu0 = kFoliageU + 0.01f;
    const float buW = (1.f - kFoliageU) - 0.02f;
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    const uint32_t stride = p.trunkSides + 1; // duplicated seam vertex

    for (uint32_t r = 0; r <= p.trunkRings; r++) {
      float t = static_cast<float>(r) / p.trunkRings;
      float y = t * H;
      float rad = trunkRadiusAt(t);
      for (uint32_t s = 0; s <= p.trunkSides; s++) {
        float a = 2.f * kPi * s / p.trunkSides;
        glm::vec3 radial(std::cos(a), 0.f, std::sin(a));
        glm::vec3 n = radial + glm::vec3(0.f, 0.1f, 0.f);
        addVertex(radial * rad + glm::vec3(0.f, y, 0.f), n,
                  {bu0 + buW * s / p.trunkSides, t},
                  {.03f * t * t, 1.f, 0.f, 1.f});
      }
    }
    for (uint32_t r = 0; r < p.trunkRings; r++)
      for (uint32_t s = 0; s < p.trunkSides; s++) {
        uint32_t a = base + r * stride + s;
        uint32_t b = a + 1;
        uint32_t c = a + stride;
        uint32_t d = c + 1;
        addTri(a, c, b);
        addTri(b, c, d);
      }
  }

  // One curved, tapered sprig card. centre(s) = origin + dir*L*s +
  // down*droop*s^2 Normals are blended toward `radialDir` so dense foliage
  // lights as a soft volume instead of showing individual flat cards.
  void addCard(glm::vec3 origin, glm::vec3 dir, glm::vec3 side, float L,
               float lift, float droop, float W, glm::vec3 radialDir,
               float tierF) {
    const uint32_t segs = p.cardSegments;
    uint32_t base = static_cast<uint32_t>(m.vertices.size());
    for (uint32_t i = 0; i <= segs; i++) {
      float s = static_cast<float>(i) / segs;
      glm::vec3 c = origin + dir * (L * s) +
                    glm::vec3(0.f, lift * s - droop * s * s, 0.f);
      glm::vec3 tang =
          glm::normalize(dir * L + glm::vec3(0.f, lift - 2.f * droop * s, 0.f));
      float w = 0.5f * W * (1.f - 0.85f * s * s);

      glm::vec3 nFace = glm::cross(tang, side);
      if (glm::dot(nFace, radialDir) < 0.f)
        nFace = -nFace;
      glm::vec3 n = glm::mix(glm::normalize(nFace), radialDir, 0.65f);

      float stiff =
          glm::clamp((0.15f + 0.85f * s) * mixf(0.8f, 1.f, tierF), 0.f, 1.f);
      float ao = mixf(0.55f, 1.f, s) * mixf(0.7f, 1.f, tierF);
      glm::vec4 col(stiff, ao, 0.f, 1.f);

      float v = s;
      addVertex(c - side * w, n, {0.005f, v}, col);
      addVertex(c + side * w, n, {kFoliageU - 0.005f, v}, col);
    }
    for (uint32_t i = 0; i < segs; i++) {
      uint32_t a = base + 2 * i, b = a + 1, c = a + 2, d = a + 3;
      addTri(a, b, c);
      addTri(b, d, c);
    }
  }

  void buildFoliage() {
    const float H = p.height;
    const glm::vec3 up(0, 1, 0);
    for (const auto &branch : branches) {
      const uint32_t K = std::max(1u, p.fanCards);
      for (uint32_t k = 0; k < K; ++k) {
        float o = float(k) - .5f * (K - 1);
        float yaw = glm::radians(p.fanYawDeg) * o;
        float roll = glm::radians(p.fanRollDeg) * o + rnd(-.1f, .1f);
        auto h = branch.direction;
        glm::vec3 d(h.x * std::cos(yaw) - h.z * std::sin(yaw), 0,
                    h.x * std::sin(yaw) + h.z * std::cos(yaw));
        auto sd = glm::normalize(glm::cross(up, d));
        auto sr = sd * std::cos(roll) + glm::cross(d, sd) * std::sin(roll);
        auto radial = glm::normalize(d * .8f + up * .3f);
        addCard(branch.origin, d, sr, branch.length, branch.lift, branch.droop,
                branch.width * (k == K / 2 ? 1.f : .85f), radial, branch.tier);
      }
    }
    // Leader: three vertical cards at the very top.
    float topY = 0.90f * H;
    for (int k = 0; k < 3; k++) {
      float a = kPi * k / 3.f;
      glm::vec3 side(std::cos(a), 0.f, std::sin(a));
      addCard(glm::vec3(0.f, topY, 0.f), up, side, 0.1f * H, 0.f, 0.f, 0.6f, up,
              1.f);
    }
  }

  void finishPart(uint32_t idxStart, uint32_t vtxStart, bool foliage) {
    Part part;
    part.indexOffset = idxStart;
    part.indexCount = static_cast<uint32_t>(m.indices.size()) - idxStart;
    part.foliage = foliage;
    glm::vec3 mn(1e30f), mx(-1e30f);
    for (size_t i = vtxStart; i < m.vertices.size(); i++) {
      mn = glm::min(mn, m.vertices[i].pos);
      mx = glm::max(mx, m.vertices[i].pos);
    }
    part.aabbMin = mn;
    part.aabbMax = mx;
    m.parts.push_back(part);
  }
};

} // namespace detail

inline Mesh generate(const Params &params = {}) {
  Mesh mesh;
  detail::Builder b(mesh, params);

  uint32_t i0 = 0, v0 = 0;
  b.planBranches();
  b.buildTrunk();
  b.buildWoodBranches();
  b.finishPart(i0, v0, false);

  i0 = static_cast<uint32_t>(mesh.indices.size());
  v0 = static_cast<uint32_t>(mesh.vertices.size());
  b.buildFoliage();
  b.finishPart(i0, v0, true);
  return mesh;
}

// RGBA8 (sRGB) atlas, size x size (size must be a multiple of 4).
// The needle sprig is 4x4 supersampled per texel, so alpha at needle edges is
// a real fractional coverage value (soft edges for alpha-to-coverage and
// bilinear magnification) rather than a hard 0/1 staircase. Transparent
// texels keep the sprig's green RGB so filtering doesn't fringe dark/pink.
inline std::vector<uint8_t> makeAtlas(uint32_t size = 1024,
                                      bool broadleaf = false) {
  using namespace detail;
  std::vector<uint8_t> px(static_cast<size_t>(size) * size * 4);
  auto put = [&](uint32_t x, uint32_t y, glm::vec3 c, float a) {
    size_t i = (static_cast<size_t>(y) * size + x) * 4;
    px[i + 0] = static_cast<uint8_t>(glm::clamp(c.r, 0.f, 1.f) * 255.f + 0.5f);
    px[i + 1] = static_cast<uint8_t>(glm::clamp(c.g, 0.f, 1.f) * 255.f + 0.5f);
    px[i + 2] = static_cast<uint8_t>(glm::clamp(c.b, 0.f, 1.f) * 255.f + 0.5f);
    px[i + 3] = static_cast<uint8_t>(glm::clamp(a, 0.f, 1.f) * 255.f + 0.5f);
  };

  const glm::vec3 darkGreen(0.09f, 0.28f, 0.16f);
  const glm::vec3 lightGreen(0.27f, 0.54f, 0.31f);
  const float N = 20.f;     // needle rows along the sprig
  const float sweep = 5.0f; // forward sweep of needles toward the tip

  // Foliage sprig at (fu, v), fu in [0,1] across the card, v = 0 at the base.
  // Returns rgb + binary alpha; supersampling below turns it into coverage.
  auto sprig = [&](float fu, float v, glm::vec3 &col) -> float {
    if (broadleaf) {
      float x = fu - .5f;
      float row = v * 9.f;
      float id = std::floor(row);
      float y = row - id - .5f;
      float side = x < 0 ? -1.f : 1.f;
      float width = .16f * (1.f - .45f * v);
      glm::vec2 q((x - side * .24f) / width, (y + std::abs(x) * .35f) / .40f);
      float serration = .055f * std::sin(std::atan2(q.y, q.x) * 7.f + id);
      float vein = 1.f - .14f * std::exp(-std::abs(q.y) * 24.f);
      col = glm::mix(darkGreen, lightGreen,
                     .35f + .55f * hash01(uint32_t(id) + 73u)) *
            vein;
      if (std::abs(x) < .012f) {
        col = {.16f, .12f, .08f};
        return 1.f;
      }
      return glm::dot(q, q) < 1.f + serration ? 1.f : 0.f;
    }
    float d = std::fabs(fu - 0.5f);
    float row = v * N - d * sweep;
    float rf = std::floor(row);
    float ph = row - rf;
    uint32_t id = static_cast<uint32_t>(rf + 64.f) * 2u + (fu > 0.5f ? 1u : 0u);
    float shade = 0.8f + 0.4f * hash01(id);
    float len =
        0.5f * (1.f - 0.45f * v) * (0.72f + 0.28f * hash01(id * 3u + 1u));
    col =
        glm::mix(darkGreen, lightGreen, glm::clamp(d / 0.5f, 0.f, 1.f)) * shade;
    if (d < 0.010f) {
      col = glm::vec3(0.16f, 0.12f, 0.08f); // twig
      return 1.f;
    }
    if (d < len) {
      float thick = 0.62f * (1.f - 0.55f * d / len);
      if (ph < thick)
        return 1.f;
    }
    return 0.f;
  };

  constexpr int SS = 4; // SS x SS samples per texel
  for (uint32_t y = 0; y < size; y++) {
    for (uint32_t x = 0; x < size; x++) {
      float u0 = static_cast<float>(x) / size;
      if (u0 < kFoliageU) {
        glm::vec3 accum(0.f);
        float cover = 0.f;
        for (int sy = 0; sy < SS; sy++)
          for (int sx = 0; sx < SS; sx++) {
            float u = (x + (sx + 0.5f) / SS) / size;
            float v = (y + (sy + 0.5f) / SS) / size;
            glm::vec3 c;
            float a = sprig(u / kFoliageU, v, c);
            accum += c * a;
            cover += a;
          }
        if (cover > 0.f)
          put(x, y, accum / cover, cover / (SS * SS));
        else
          put(x, y, darkGreen * 1.2f, 0.f); // keep fringe colour sane
      } else {
        float u = (x + 0.5f) / size;
        float v = (y + 0.5f) / size;
        float bu = (u - kFoliageU) / (1.f - kFoliageU);
        float streak = hash01(static_cast<uint32_t>(bu * 20.f) * 7919u + 13u);
        float fine = hash01(x * 73856093u ^ y * 19349663u);
        float ridge = 0.5f + 0.5f * std::sin(bu * 60.f + streak * 6.f);
        float br =
            0.70f + 0.30f * streak + 0.10f * (fine - 0.5f) + 0.1f * ridge;
        (void)v;
        put(x, y, glm::vec3(0.30f, 0.20f, 0.13f) * br, 1.f);
      }
    }
  }
  return px;
}

// Uploads a generated fir as a Scene<TreeSys::Vertex> ready for
// TreeSys::addVariant({scene, {false, true}}). Mesh 0 = trunk, mesh 1 =
// foliage, both using material 0 (the atlas, texture index 0).
//
// `texturesSetLayout` must be TreeSys::texturesSetLayout so the scene's
// sceneTextureSet (set 1) is allocated against the layout the tree pipeline
// expects.
inline std::shared_ptr<Scene<TreeSys::Vertex>>
makeScene(EngineContext &context, vk::DescriptorSetLayout texturesSetLayout,
          const Params &params = {}, uint32_t atlasSize = 1024) {
  Mesh gen = generate(params);
  // The atlas doesn't depend on the seed, so build it once per size and reuse
  // it for every variant (1024^2 x 16 samples is ~100+ ms).
  static std::map<std::pair<uint32_t, bool>, std::vector<uint8_t>> atlasCache;
  auto key = std::pair(atlasSize, params.broadleaf);
  auto it = atlasCache.find(key);
  if (it == atlasCache.end())
    it = atlasCache.emplace(key, makeAtlas(atlasSize, params.broadleaf)).first;
  std::vector<uint8_t> atlas = it->second;

  SceneCreateInfo<TreeSys::Vertex> info{gen.vertices, gen.indices};
  info.texturePixels = std::move(atlas);
  info.textureSize = {atlasSize, atlasSize};

  auto scene = std::make_shared<Scene<TreeSys::Vertex>>(context, info,
                                                        texturesSetLayout);
  scene->materials[0].roughnessFactor = 0.9f;
  scene->materials[0].metallicFactor = glm::vec4(0.f);

  // The ctor creates one catch-all mesh with no AABB; replace it with our
  // two parts (TreeSys culls per mesh using mesh.aabb).
  scene->meshes.clear();
  for (const Part &part : gen.parts) {
    auto &m = scene->meshes.emplace_back();
    m.aabb.min = part.aabbMin;
    m.aabb.max = part.aabbMax;
    auto &prim = m.primitives.emplace_back();
    prim.indexOffset = part.indexOffset;
    prim.indexCount = part.indexCount;
    prim.materialIndex = 0;
  }
  return scene;
}

} // namespace vkh::fir
