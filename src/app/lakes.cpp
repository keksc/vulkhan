#include "lakes.hpp"
#include "../vkh/systems/planarReflectionSys.hpp"
#include "../vkh/systems/terrain/terrain.hpp"
#include "../vkh/systems/tree/treeStream.hpp"
#include "../vkh/systems/water/water.hpp"
#include "math.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <future>
#include <map>
namespace app {
struct LakeStreaming::Impl {
  vkh::EngineContext &context;
  vkh::SkySys &skySys;
  vkh::WSTessendorf &waveSim;
  vkh::TerrainSys &terrainSys;
  const vkh::mountain::MountainCreateInfo &mountainInfo;
  std::map<std::pair<int32_t, int32_t>, std::vector<vkh::terrain::RefineZone>>
      tileRefineZones;
  struct LakeTile {
    std::future<std::vector<vkh::mountain::Lake>> fut;
    bool ready{false};
    std::vector<vkh::mountain::Lake> lakes;
    std::vector<char> active; // per lake: has a live WaterSys
  };
  struct DeadLake {
    std::unique_ptr<vkh::WaterSys> water;
    uint64_t freeAt;
  };
  static constexpr float kReflectionPlaneMergeEps = 0.05f; // metres
  static constexpr float kLakeLoadDist =
      2200.f; // create lakes closer than this
  static constexpr float kLakeUnloadDist =
      2600.f;                        // destroy lakes farther than this
  std::list<ActiveLake> activeLakes; // stable addresses
  std::vector<std::unique_ptr<ReflectionGroup>> reflectionGroups;
  std::deque<DeadLake> deadLakes;
  std::vector<std::unique_ptr<vkh::WaterSys>> freeWaters; // recycled
  std::map<std::pair<int32_t, int32_t>, LakeTile> lakeTiles;
  uint64_t lakeFrame = 0;
  const float lakeTileSize;

  // Lake footprints, readable from the tree worker threads, so trees never
  // grow in a lake. Tree tiles also wait until every lake tile they overlap
  // has been analysed (see the forest section).
  vkh::trees::ExclusionZones lakeZones;
  std::function<float(glm::vec2)> coarseHeight;
  Impl(vkh::EngineContext &context, vkh::SkySys &skySys,
       vkh::WSTessendorf &waveSim, vkh::TerrainSys &terrainSys,
       const vkh::mountain::MountainCreateInfo &mountainInfo,
       std::vector<vkh::mountain::Lake> initial)
      : context(context), skySys(skySys), waveSim(waveSim),
        terrainSys(terrainSys), mountainInfo(mountainInfo),
        lakeTileSize(mountainInfo.worldSize.x), lakeZones(lakeTileSize),
        coarseHeight(vkh::mountain::makeCoarseHeightFn(mountainInfo, 12.f)) {
    auto &central = lakeTiles[{0, 0}];
    central.ready = true;
    central.lakes = std::move(initial);
    central.active.assign(central.lakes.size(), 0);
    registerLakes(0, 0, central.lakes);
    updateLakes(context.camera.position, SIZE_MAX);
  }
  vkh::terrain::RefineZone lakeRefineZone(const vkh::mountain::Lake &l) {
    const float pad = std::max(8.f, 4.f * l.meshCellSize);
    const glm::vec2 c(l.worldCenter.x, l.worldCenter.z);
    const glm::vec2 h = l.worldSize * 0.5f + pad;
    return vkh::terrain::RefineZone{c - h, c + h};
  }
  void pushRefineZones() {
    std::vector<vkh::terrain::RefineZone> all;
    for (const auto &[tile, zs] : tileRefineZones)
      all.insert(all.end(), zs.begin(), zs.end());
    terrainSys.setLakeZones(std::move(all));
  }
  void registerLakes(int32_t tx, int32_t tz,
                     const std::vector<vkh::mountain::Lake> &lakes) {
    lakeZones.clearTile(tx, tz);
    for (const auto &l : lakes)
      lakeZones.add({tx, tz}, {l.worldCenter.x, l.worldCenter.z}, l.worldSize,
                    l.cellMask, l.maskResolution);
    lakeZones.markTileAnalysed(tx, tz);

    auto &zs = tileRefineZones[{tx, tz}];
    zs.clear();
    for (const auto &l : lakes)
      zs.push_back(lakeRefineZone(l));
    pushRefineZones();
  }
  void createLake(std::pair<int32_t, int32_t> tile, size_t index) {
    LakeTile &t = lakeTiles.at(tile);
    const vkh::mountain::Lake &lake = t.lakes[index];
    vkh::WaterCreateInfo lakeInfo{};
    lakeInfo.center = lake.worldCenter;
    lakeInfo.size = lake.worldSize;
    const float nativeWaveSpacing =
        vkh::WSTessendorf::tileLength / vkh::WSTessendorf::tileSize;
    const float cellWorldWidth = std::min(lake.worldSize.x, lake.worldSize.y) /
                                 32.f; // unchanged wave damping reference
    const float smallLakeDamping =
        std::clamp(cellWorldWidth / nativeWaveSpacing, 0.15f, 1.0f);
    // Let the FFT spectrum supply the visible wave detail. Small lakes
    // remain calmer; the vertex displacement limits guard against folds.
    lakeInfo.waveScale = 0.35f * smallLakeDamping;
    lakeInfo.chopScale = 0.10f * smallLakeDamping;
    lakeInfo.animSpeed = 1.f;
    // The FFT models a 1000m patch. Compressing it into 15m made the
    // waves smaller than the mesh spacing, so mip filtering erased them.
    lakeInfo.uvRepeat = lake.worldSize / vkh::WSTessendorf::tileLength;
    lakeInfo.meshXZ = lake.meshXZ;
    lakeInfo.meshDepth = lake.meshDepth;
    lakeInfo.meshCellSize = lake.meshCellSize;
    lakeInfo.meshIndices = lake.meshIndices;
    std::unique_ptr<vkh::WaterSys> water;
    if (!freeWaters.empty()) { // recycle: no pipeline/descriptor churn
      water = std::move(freeWaters.back());
      freeWaters.pop_back();
      water->reconfigure(lakeInfo);
    } else {
      water =
          std::make_unique<vkh::WaterSys>(context, skySys, waveSim, lakeInfo);
    }

    ReflectionGroup *group = nullptr;
    size_t groupIdx = 0;
    for (size_t g = 0; g < reflectionGroups.size(); ++g)
      if (!reflectionGroups[g]->lakes.empty() &&
          std::abs(reflectionGroups[g]->planeY - lake.worldCenter.y) <=
              kReflectionPlaneMergeEps) {
        group = reflectionGroups[g].get();
        groupIdx = g;
        break;
      }
    if (!group) {
      for (size_t g = 0; g < reflectionGroups.size(); ++g)
        if (reflectionGroups[g]->lakes.empty()) { // reuse an idle target
          group = reflectionGroups[g].get();
          groupIdx = g;
          break;
        }
    }
    if (!group) {
      // 640x360: the water's ripple distortion hides the softness.
      auto g = std::make_unique<ReflectionGroup>();
      g->refl = std::make_unique<vkh::PlanarReflectionSys>(
          context, glm::uvec2{640, 360});
      reflectionGroups.push_back(std::move(g));
      group = reflectionGroups.back().get();
      groupIdx = reflectionGroups.size() - 1;
    }
    group->planeY = lake.worldCenter.y;
    water->setPlanarReflectionSource(group->refl.get());
    activeLakes.push_back(
        ActiveLake{lake, std::move(water), tile, index, groupIdx});
    group->lakes.push_back(&activeLakes.back());
    t.active[index] = 1;
  }
  void retireLake(std::list<ActiveLake>::iterator it) {
    auto &gl = reflectionGroups[it->group]->lakes;
    gl.erase(std::remove(gl.begin(), gl.end(), &*it), gl.end());
    auto tIt = lakeTiles.find(it->tile);
    if (tIt != lakeTiles.end() && it->index < tIt->second.active.size())
      tIt->second.active[it->index] = 0;
    // Frames in flight may still reference the water's GPU resources.
    deadLakes.push_back({std::move(it->water),
                         lakeFrame + context.vulkan.maxFramesInFlight + 2});
    activeLakes.erase(it);
  }
  float tileDist(const glm::vec2 &cam, int32_t tx, int32_t tz) {
    float dx =
        std::max(std::abs(cam.x - static_cast<float>(tx) * lakeTileSize) -
                     0.5f * lakeTileSize,
                 0.f);
    float dz =
        std::max(std::abs(cam.y - static_cast<float>(tz) * lakeTileSize) -
                     0.5f * lakeTileSize,
                 0.f);
    return std::sqrt(dx * dx + dz * dz);
  }
  float lakeDist(const glm::vec2 &cam, const vkh::mountain::Lake &l) {
    return glm::distance(cam, glm::vec2(l.worldCenter.x, l.worldCenter.z)) -
           0.5f * glm::length(l.worldSize);
  }
  void updateLakes(const glm::vec3 &camPos3, size_t budget) {
    ++lakeFrame;
    const glm::vec2 cam(camPos3.x, camPos3.z);
    while (!deadLakes.empty() && deadLakes.front().freeAt <= lakeFrame) {
      freeWaters.push_back(std::move(deadLakes.front().water));
      deadLakes.pop_front();
    }

    // Retire lakes that are too far away.
    for (auto it = activeLakes.begin(); it != activeLakes.end();) {
      auto next = std::next(it);
      if (lakeDist(cam, it->lake) > kLakeUnloadDist)
        retireLake(it);
      it = next;
    }

    // Kick off analysis of tiles near the camera.
    const int32_t cx = static_cast<int32_t>(std::lround(cam.x / lakeTileSize));
    const int32_t cz = static_cast<int32_t>(std::lround(cam.y / lakeTileSize));
    const int32_t r =
        static_cast<int32_t>(std::ceil(kLakeLoadDist / lakeTileSize)) + 1;
    // Fast travel must not launch four new expensive terrain analyses on
    // every frame. Bound concurrent jobs as well as launches per frame.
    const int inFlight = std::count_if(
        lakeTiles.begin(), lakeTiles.end(), [](const auto &entry) {
          return !entry.second.ready && entry.second.fut.valid();
        });
    int launches = std::max(0, std::min(1, 3 - inFlight));
    for (int32_t tz = cz - r; tz <= cz + r && launches > 0; ++tz)
      for (int32_t tx = cx - r; tx <= cx + r && launches > 0; ++tx) {
        if (tileDist(cam, tx, tz) > kLakeLoadDist ||
            lakeTiles.contains({tx, tz}))
          continue;
        lakeTiles[{tx, tz}].fut =
            std::async(std::launch::async, [this, tx, tz] {
              return vkh::mountain::findLakesInTile(mountainInfo, tx, tz);
            });
        --launches;
      }

    // Collect finished tiles, create lakes (a few per frame), drop old tiles.
    for (auto it = lakeTiles.begin(); it != lakeTiles.end();) {
      LakeTile &t = it->second;
      if (!t.ready && t.fut.valid() &&
          t.fut.wait_for(std::chrono::seconds(0)) ==
              std::future_status::ready) {
        t.lakes = t.fut.get();
        t.active.assign(t.lakes.size(), 0);
        t.ready = true;
        registerLakes(it->first.first, it->first.second, t.lakes);
      }
      if (t.ready) {
        for (size_t i = 0; i < t.lakes.size() && budget > 0; ++i)
          if (!t.active[i] && lakeDist(cam, t.lakes[i]) < kLakeLoadDist) {
            createLake(it->first, i);
            --budget;
          }
        const bool anyActive = std::any_of(t.active.begin(), t.active.end(),
                                           [](char c) { return c; });
        if (it->first != std::pair<int32_t, int32_t>{0, 0} && !anyActive &&
            tileDist(cam, it->first.first, it->first.second) >
                kLakeUnloadDist) {
          lakeZones.clearTile(it->first.first, it->first.second);
          tileRefineZones.erase(it->first);
          pushRefineZones();
          it = lakeTiles.erase(it);
          continue;
        }
      }
      ++it;
    }
  }
  bool rayBlocked(const glm::vec3 &from, const glm::vec3 &to) {
    const glm::vec2 d(to.x - from.x, to.z - from.z);
    const float dist = glm::length(d);
    if (dist < 1.f)
      return false;
    const int steps = std::clamp(static_cast<int>(dist / 25.f), 8, 48);
    const float margin = 3.f + 0.01f * dist;
    for (int i = 1; i < steps; ++i) {
      const float t = static_cast<float>(i) / static_cast<float>(steps);
      const glm::vec2 p(from.x + d.x * t, from.z + d.y * t);
      const float rayY = glm::mix(from.y, to.y, t);
      if (coarseHeight(p) > rayY + margin)
        return true;
    }
    return false;
  }
  bool lakeOccluded(const ActiveLake &al, const glm::vec3 &cam) {
    const auto &l = al.lake;
    const glm::vec2 h = l.worldSize * 0.45f;
    for (int j = -1; j <= 1; ++j)
      for (int i = -1; i <= 1; ++i) {
        const glm::vec3 target(l.worldCenter.x + h.x * static_cast<float>(i),
                               l.worldCenter.y + 1.f,
                               l.worldCenter.z + h.y * static_cast<float>(j));
        if (!rayBlocked(cam, target))
          return false;
      }
    return true;
  }
  void updateLakeVisibility(const std::array<glm::vec4, 6> &frustum,
                            const glm::vec3 &cam) {
    constexpr uint64_t kResultTTL = 45; // frames before a result expires
    constexpr int kTestsPerFrame = 3;
    std::vector<ActiveLake *> due;
    for (auto &al : activeLakes) {
      const float radius = glm::length(al.lake.worldSize) * 0.5f + 5.f;
      const bool inFrustum =
          sphereInFrustum(frustum, al.lake.worldCenter, radius);
      const bool closeBy =
          glm::distance(glm::vec2(cam.x, cam.z),
                        glm::vec2(al.lake.worldCenter.x,
                                  al.lake.worldCenter.z)) < radius + 100.f;
      if (!inFrustum) {
        al.visible = false;
        continue;
      }
      if (closeBy) { // standing in/next to it: always draw
        al.occluded = false;
        al.testedFrame = lakeFrame;
      } else if (al.testedFrame == 0 ||
                 lakeFrame - al.testedFrame > kResultTTL) {
        al.occluded = false; // unknown => visible
      }
      if (!closeBy &&
          (al.testedFrame == 0 || lakeFrame - al.testedFrame > kResultTTL / 3))
        due.push_back(&al);
      al.visible = !al.occluded;
    }
    std::sort(due.begin(), due.end(),
              [](const ActiveLake *a, const ActiveLake *b) {
                return a->testedFrame < b->testedFrame;
              });
    for (int i = 0; i < kTestsPerFrame && i < static_cast<int>(due.size());
         ++i) {
      due[i]->occluded = lakeOccluded(*due[i], cam);
      due[i]->testedFrame = lakeFrame;
      due[i]->visible = !due[i]->occluded;
    }
  }
};
LakeStreaming::LakeStreaming(vkh::EngineContext &c, vkh::SkySys &s,
                             vkh::WSTessendorf &w, vkh::TerrainSys &t,
                             const vkh::mountain::MountainCreateInfo &m,
                             std::vector<vkh::mountain::Lake> initial)
    : impl(std::make_unique<Impl>(c, s, w, t, m, std::move(initial))) {}
LakeStreaming::~LakeStreaming() = default;
void LakeStreaming::update(const glm::vec3 &p, size_t b) {
  impl->updateLakes(p, b);
}
void LakeStreaming::updateVisibility(const std::array<glm::vec4, 6> &f,
                                     const glm::vec3 &p) {
  impl->updateLakeVisibility(f, p);
}
vkh::trees::ExclusionZones &LakeStreaming::zones() { return impl->lakeZones; }
std::list<ActiveLake> &LakeStreaming::active() { return impl->activeLakes; }
std::vector<std::unique_ptr<ReflectionGroup>> &LakeStreaming::groups() {
  return impl->reflectionGroups;
}
uint64_t LakeStreaming::frame() const { return impl->lakeFrame; }
float LakeStreaming::distance(const glm::vec2 &p,
                              const vkh::mountain::Lake &l) const {
  return impl->lakeDist(p, l);
}
void configureInitialLakeZones(vkh::TerrainSys &terrain,
                               const std::vector<vkh::mountain::Lake> &lakes) {
  std::vector<vkh::terrain::RefineZone> zones;
  for (const auto &l : lakes) {
    float pad = std::max(8.f, 4.f * l.meshCellSize);
    glm::vec2 c{l.worldCenter.x, l.worldCenter.z}, h = l.worldSize * .5f + pad;
    zones.push_back({c - h, c + h});
  }
  terrain.setLakeZones(std::move(zones));
}
} // namespace app
