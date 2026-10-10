#include "application.hpp"
#include "assets.hpp"
#include "lakes.hpp"
#include "math.hpp"
#include "renderFrame.hpp"
#include "streamLighting.hpp"
#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "../UI.hpp"
#include "../boids.hpp"
#include "../modding.hpp"
#include "../mountain.hpp"
#include "../settings.hpp"
#include "../vkh/audio.hpp"
#include "../vkh/camera.hpp"
#include "../vkh/cleanup.hpp"
#include "../vkh/cpuProfiler.hpp"
#include "../vkh/engineContext.hpp"
#include "../vkh/gpuProfiler.hpp"
#include "../vkh/init.hpp"
#include "../vkh/input.hpp"
#include "../vkh/paths.hpp"
#include "../vkh/renderer.hpp"
#include "../vkh/swapChain.hpp"
#include "../vkh/systems/entity/entities.hpp"
#include "../vkh/systems/grass/proceduralGrass.hpp"
#include "../vkh/systems/particleEnvironment.hpp"
#include "../vkh/systems/particles.hpp"
#include "../vkh/systems/planarReflectionSys.hpp"
#include "../vkh/systems/postProcessing.hpp"
#include "../vkh/systems/sky.hpp"
#include "../vkh/systems/smoke/smoke.hpp"
#include "../vkh/systems/terrain/terrain.hpp"
#include "../vkh/systems/tree/rockFootprints.hpp"
#include "../vkh/systems/tree/treeStream.hpp"
#include "../vkh/systems/tree/treeSys.hpp"
#include "../vkh/systems/water/water.hpp"
#include "../vkh/systems/wolf/wolfSys.hpp"
#include "../vkh/window.hpp"
#include "wolfStudio.hpp"

#include "../network/networkSession.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <deque>
#include <future>
#include <limits>
#include <list>
#include <map>
#include <print>
#include <random>
#include <string>
#include <unordered_map>

namespace app {
void run() {
  paths::initCacheDir("vulkhan");
  paths::setWorkingDirectoryToExecutable();
  vkh::settings::load();
  vkh::settings::validateSkyResolution();
  vkh::settings::validateGraphics();

  if (const char *studio = std::getenv("VULKHAN_WOLF_STUDIO");
      studio && studio[0] == '1') {
    runWolfStudio();
    return;
  }
  vkh::EngineContext context{};
  const char *worldPath = std::getenv("VULKHAN_WORLD_CONFIG");
  auto worldConfig =
      vkh::world::loadConfig(worldPath ? worldPath : "config/world.toml");
  context.worldConfig = worldConfig;
  vkh::initWindow(context);
  vkh::init(context);
  vkh::audio::init();
  vkh::input::init(context);

  vkh::renderer::init(context);

  {
    ModManager modMgr;

    NetworkSession netSession("127.0.0.1");

    std::chrono::time_point<std::chrono::high_resolution_clock> audioFadeBegin;
    const float audioFadeSpeed = 2.5f;

    // vkh::audio::Sound boringSpeech("sounds/Rhorhorho.opus");
    // boringSpeech.play();
    // vkh::audio::Sound bgm("sounds/Enter Remollon.opus");
    // bgm.play();

    vkh::SkySys skySys(context, vkh::settings::current().skyMilkyWayFaceSize,
                       vkh::settings::current().skyDiscTurbulenceSize,
                       vkh::settings::current().useCachedSkyBake);
    vkh::EntitySys entitySys(context);
    // One TreeSys for the whole forest. LOD tiers of each species are separate
    // variants with pass masks (see below), so main view and planar
    // reflections share one pipeline, one set of buffers and one cull.
    vkh::TreeSys treeSys(context);
    treeSys.foliageTint = [worldConfig](glm::vec3 p) {
      float h = glm::clamp((p.y - worldConfig->terrain.baseHeight) /
                               (worldConfig->terrain.peakHeight -
                                worldConfig->terrain.baseHeight),
                           0.f, 1.f);
      return glm::clamp(worldConfig->sample({p.x, p.z}, h).foliage /
                            glm::vec3(.12f, .25f, .07f),
                        glm::vec3(0), glm::vec3(8));
    };
    treeSys.rockTint = [worldConfig](glm::vec3 p) {
      float h = glm::clamp((p.y - worldConfig->terrain.baseHeight) /
                               (worldConfig->terrain.peakHeight -
                                worldConfig->terrain.baseHeight),
                           0.f, 1.f);
      return worldConfig->sample({p.x, p.z}, h).rock / glm::vec3(.25f);
    };

    vkh::SmokeSys smokeSys(context);

    vkh::WSTessendorf waveSim(context);
    // The spectrum reads the shared physical wind speed. Gusts animate
    // foliage without forcing an expensive spectrum rebuild every frame.
    waveSim.warmUp();

    vkh::ParticleSys particleSys(context);

    auto postProcessing = createPostProcessing(context, smokeSys);
    auto &postProcessingSys = *postProcessing;

    auto &entities = entitySys.entities;
    // generateDungeon(context, entitySys);
    // auto houseScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/haunted_victorian_house.glb",
    //     entitySys.texturesSetLayout);
    // houseScene->uploadModelGPU(entitySys.texturesSetLayout);
    // for (size_t i = 0; i < houseScene->meshes.size(); i++)
    //   entities.emplace_back(vkh::EntitySys::Transform{},
    //                         vkh::EntitySys::RigidBody{}, houseScene, i);

    auto birdScene = entitySys.loadScene("models/paperplane.glb");

    vkh::mountain::MountainCreateInfo mountainInfo{};
    mountainInfo.treeDensity = 0.5f;
    mountainInfo.resolution = glm::uvec2{512u};
    mountainInfo.worldSize = glm::vec2{800.f};
    mountainInfo.baseHeight = worldConfig->terrain.baseHeight;
    mountainInfo.peakHeight = worldConfig->terrain.peakHeight;
    mountainInfo.baseFeatureWorldSize = worldConfig->terrain.featureSize;
    mountainInfo.octaves = worldConfig->terrain.erosionOctaves;
    mountainInfo.erosionScale = worldConfig->terrain.erosionScale;
    mountainInfo.gullyWeight = 0.5f;
    mountainInfo.erosionStrength = worldConfig->terrain.erosionStrength;
    mountainInfo.heightModifier = [worldConfig](glm::vec2 xz, float y) {
      return worldConfig->shapeHeight(xz, y);
    };
    mountainInfo.lakeAllowed = [worldConfig](glm::vec3 p) {
      return worldConfig->lakeAllowed(p);
    };
    auto featureHeight = vkh::mountain::makeHeightFn(mountainInfo);
    for (auto &volcano : worldConfig->volcanoes)
      volcano.position.y =
          featureHeight({volcano.position.x, volcano.position.z},
                        vkh::mountain::terrainCellSize(mountainInfo));
    const auto worldGpu = worldConfig->gpuData();
    context.vulkan.worldUbo->write(&worldGpu, sizeof(worldGpu));
    // Optional authoring preview, leaving normal player controls unchanged.
    bool previewMode = false;
    bool wolfPreviewMode = false;
    glm::vec3 previewPosition{};
    glm::quat previewOrientation{1, 0, 0, 0};
    if (const char *mode = std::getenv("VULKHAN_PREVIEW")) {
      glm::vec3 target{};
      if (std::string(mode) == "volcano" && !worldConfig->volcanoes.empty()) {
        const auto &v = worldConfig->volcanoes.front();
        previewPosition = {v.position.x - v.radius * .9f,
                           v.position.y + v.depth + 60.f,
                           v.position.z + v.radius * 1.8f};
        target = v.position + glm::vec3(0, v.plumeHeight * .2f, 0);
        previewMode = true;
      } else if (std::string(mode) == "autumn") {
        for (const auto &r : worldConfig->regions)
          if (worldConfig->profiles[r.profile].name == "autumn") {
            glm::vec2 xz =
                r.center + glm::vec2(-r.radius * .15f, r.radius * .25f);
            previewPosition = {xz.x,
                               featureHeight(xz, vkh::mountain::terrainCellSize(
                                                     mountainInfo)) +
                                   22.f,
                               xz.y};
            target = {r.center.x,
                      featureHeight(r.center, vkh::mountain::terrainCellSize(
                                                  mountainInfo)) +
                          12.f,
                      r.center.y};
            previewMode = true;
            break;
          }
      }
      if (previewMode) {
        previewOrientation = glm::quatLookAt(
            glm::normalize(target - previewPosition), glm::vec3(0, 1, 0));
        context.camera.position = previewPosition;
        context.camera.orientation = previewOrientation;
        vkh::camera::calcViewYXZ(context);
      }
    }

    vkh::mountain::MountainHeightmap mountainHeightmap;
    std::vector<vkh::mountain::Lake> mountainLakes;
    // Trees are streamed over the whole infinite terrain (see the forest
    // section below), so the finite mountain no longer scatters its own.
    mountainInfo.generateTrees = false;
    mountainInfo.buildMesh = false;
    vkh::mountain::generate(context, entitySys.texturesSetLayout, mountainInfo,
                            &mountainHeightmap, &mountainLakes, nullptr);
    // generate() is still used for the heightmap (shadows/AO) and lakes, but
    // its mesh is not drawn: TerrainSys streams chunks straight from the
    // height function instead.
    vkh::TerrainSys::CreateInfo terrainInfo{};
    terrainInfo.heightFn = vkh::mountain::makeHeightFn(mountainInfo);
    terrainInfo.baseHeight = mountainInfo.baseHeight;
    terrainInfo.heightRange = mountainInfo.peakHeight - mountainInfo.baseHeight;
    for (const auto &volcano : worldConfig->volcanoes)
      terrainInfo.extraHeightRange += std::max(volcano.height, volcano.depth);
    terrainInfo.rockSlopeAngleDeg = mountainInfo.rockSlopeAngleDeg;
    // Same vertex spacing as the finite mountain => identical LOD 0 detail
    // (and the detail the lake meshes are fitted to).
    terrainInfo.lod0CellSize = vkh::mountain::terrainCellSize(mountainInfo);
    terrainInfo.heightmap = {mountainHeightmap.image,
                             mountainHeightmap.worldMin,
                             mountainHeightmap.worldSize};
    vkh::TerrainSys terrainSys(context, terrainInfo);

    configureInitialLakeZones(terrainSys, mountainLakes);
    terrainSys.prewarm(context.camera.position);

    registerSceneryAssets(context, treeSys, mountainInfo.seed);

    LakeStreaming lakes(context, skySys, waveSim, terrainSys, mountainInfo,
                        std::move(mountainLakes));
    auto &lakeZones = lakes.zones();

    // ---- Streaming forest ----------------------------------------------
    // Trees are placed per 160 m tile from the same height function the
    // terrain meshes use (deterministic, generated on worker threads) with
    // the same altitude/slope/density rules the finite mountain used. Each
    // tile gets a distance tier -> mesh LOD (see the variant registration
    // above). A rebuild of the GPU instance data runs on a worker thread and
    // is swapped in when ready, so a camera crossing a tile boundary never
    // stalls a frame.
    constexpr float kTreeSinkMid = 0.5f; // metres: coarse terrain LODs
    constexpr float kTreeSinkFar = 1.0f; // under-approximate fine relief
    vkh::trees::Params treeParams;
    treeParams.speciesCount = kFirSpecies;
    treeParams.cellSize = std::max(mountainInfo.treeMinSpacing, 3.f);
    treeParams.nearDist = 140.f; // high-poly
    treeParams.midDist = 450.f;  // low-poly, also in reflections
    treeParams.farDist = 1000.f; // low-poly, main view only; load radius
    treeParams.seed = mountainInfo.seed ^ 0x7ee5eedu;

    const auto treeHeightFn = vkh::mountain::makeHeightFn(mountainInfo);
    const float treeCell = terrainInfo.lod0CellSize;
    const float treeBase = mountainInfo.baseHeight;
    const float treeRange = mountainInfo.peakHeight - mountainInfo.baseHeight;
    const float treeMinAlt = mountainInfo.treeMinAltitude;
    const float treeMaxAlt = mountainInfo.treeMaxAltitude;
    const float treeMaxSlope = mountainInfo.treeMaxSlopeNorm;
    const float treeDensity = mountainInfo.treeDensity;
    const float cosRockSlope =
        std::cos(glm::radians(mountainInfo.rockSlopeAngleDeg));
    constexpr float kShoreMargin = 4.f; // keep trees this far from the water

    // Same acceptance rules as mountain::generate()'s tree scatter, evaluated
    // on demand: altitude band, slope limit, smooth fades, lake exclusion.
    // Cheap rejections first - each height sample is a full erosion eval.
    auto treeSample = [=, &lakeZones](glm::vec2 xz, float &y) -> float {
      y = treeHeightFn(xz, treeCell);
      const float hNorm = (y - treeBase) / treeRange;
      if (hNorm < treeMinAlt || hNorm > treeMaxAlt)
        return 0.f;
      if (lakeZones.blocked(xz, kShoreMargin))
        return 0.f;
      const float hx = treeHeightFn(xz + glm::vec2(treeCell, 0.f), treeCell);
      const float hz = treeHeightFn(xz + glm::vec2(0.f, treeCell), treeCell);
      const float dHdx = (hx - y) / treeCell;
      const float dHdz = (hz - y) / treeCell;
      const float normalY = 1.f / std::sqrt(1.f + dHdx * dHdx + dHdz * dHdz);
      float slopeNorm = 0.f;
      if (cosRockSlope < 1.f - 1e-5f)
        slopeNorm =
            glm::clamp((1.f - normalY) / (1.f - cosRockSlope), 0.f, 1.f);
      if (slopeNorm > treeMaxSlope)
        return 0.f;
      const float altitudeFade =
          glm::smoothstep(treeMaxAlt, treeMaxAlt * 0.7f, hNorm) *
          glm::smoothstep(treeMinAlt, treeMinAlt * 1.5f + 0.02f, hNorm);
      const float slopeFade =
          1.f - glm::smoothstep(treeMaxSlope * 0.5f, treeMaxSlope, slopeNorm);
      return glm::clamp(treeDensity * altitudeFade * slopeFade *
                            worldConfig->sample(xz, hNorm).treeDensity,
                        0.f, 1.f);
    };
    vkh::trees::TreeStreamer treeStreamer(
        treeParams, treeSample, [&lakeZones](glm::vec2 a, glm::vec2 b) {
          return lakeZones.tilesReady(a, b);
        });

    // Sparse, clustered stones use the existing worker/instance/culling path.
    // Only a small near field is prewarmed; distant rocks stream
    // asynchronously.
    vkh::trees::Params rockParams;
    rockParams.speciesCount = kRockSpecies;
    rockParams.cellSize = 5.f;
    rockParams.nearDist = 90.f;
    rockParams.midDist = 220.f;
    rockParams.farDist = 320.f;
    rockParams.unloadMargin = 100.f;
    rockParams.scaleMin = 0.35f;
    rockParams.scaleMax = 1.6f;
    rockParams.seed = mountainInfo.seed ^ 0x51a7e12u;
    auto rockSample = [=, &lakeZones](glm::vec2 xz, float &y) -> float {
      if (lakeZones.blocked(xz, 2.f))
        return 0.f;
      y = treeHeightFn(xz, treeCell);
      const float hx = treeHeightFn(xz + glm::vec2(treeCell, 0), treeCell);
      const float hz = treeHeightFn(xz + glm::vec2(0, treeCell), treeCell);
      const float slope = glm::length(glm::vec2(hx - y, hz - y)) / treeCell;
      if (slope > 1.1f)
        return 0.f;
      const float cluster =
          0.5f + 0.5f * std::sin(xz.x * 0.023f) * std::sin(xz.y * 0.031f);
      return (0.06f + 0.22f * glm::smoothstep(0.05f, 0.6f, slope)) * cluster;
    };
    vkh::trees::TreeStreamer rockStreamer(
        rockParams, rockSample, [&lakeZones](glm::vec2 a, glm::vec2 b) {
          return lakeZones.tilesReady(a, b);
        });
    struct ScenerySnapshot {
      vkh::trees::Snapshot trees, rocks;
    };
    vkh::rocks::Footprints rockFootprints;
    uint64_t rockFootprintVersion{0};
    auto updateRockFootprints = [&](const vkh::trees::Snapshot &snapshot) {
      std::vector<vkh::rocks::Footprint> discs;
      for (const auto &tile : snapshot)
        for (const auto &rock : *tile.trees)
          discs.push_back(
              {{rock.position.x, rock.position.z}, rock.scale * 0.65f});
      rockFootprints.replace(discs);
      ++rockFootprintVersion;
    };

    // Snapshot of tiles -> TreeSys placements (tier picks the LOD variant).
    // Runs on a worker thread: reads only the snapshot and TreeSys's
    // immutable variant list.
    using TreeSet = std::shared_ptr<const vkh::TreeSys::InstanceSet>;
    auto buildTreeSet = [&treeSys,
                         worldConfig](ScenerySnapshot snap) -> TreeSet {
      using vkh::trees::Tier;
      size_t n = 0;
      for (const auto &tile : snap.trees)
        n += tile.trees->size() * (tile.tier == Tier::Near ? 2u : 1u);
      for (const auto &tile : snap.rocks)
        n += tile.trees->size();
      std::vector<vkh::TreeSys::TreePlacement> placements;
      placements.reserve(n);
      for (const auto &tile : snap.trees)
        for (const auto &t : *tile.trees) {
          float altitude =
              glm::clamp((t.position.y - worldConfig->terrain.baseHeight) /
                             (worldConfig->terrain.peakHeight -
                              worldConfig->terrain.baseHeight),
                         0.f, 1.f);
          float broadleaf =
              worldConfig->sample({t.position.x, t.position.z}, altitude)
                  .broadleaf;
          float choice =
              vkh::biome::cell(int32_t(std::floor(t.position.x)),
                               int32_t(std::floor(t.position.z)), 0x91e7u);
          const uint32_t sp =
              t.species + (choice < broadleaf ? kFirSpecies : 0);
          switch (tile.tier) {
          case Tier::Near:
            placements.push_back(
                {t.position, t.yaw, t.scale, kTierHigh * kTreeSpecies + sp});
            placements.push_back(
                {t.position, t.yaw, t.scale, kTierLowRefl * kTreeSpecies + sp});
            break;
          case Tier::Mid: {
            glm::vec3 p = t.position;
            p.y -= kTreeSinkMid;
            placements.push_back(
                {p, t.yaw, t.scale, kTierLowBoth * kTreeSpecies + sp});
            break;
          }
          case Tier::Far: {
            glm::vec3 p = t.position;
            p.y -= kTreeSinkFar;
            placements.push_back(
                {p, t.yaw, t.scale, kTierLowMain * kTreeSpecies + sp});
            break;
          }
          }
        }
      for (const auto &tile : snap.rocks)
        for (const auto &rock : *tile.trees)
          placements.push_back(
              {rock.position - glm::vec3(0.f, rock.scale * 0.2f, 0.f), rock.yaw,
               rock.scale, kRockVariantBase + rock.species});
      return treeSys.buildInstanceSet(placements);
    };
    vkh::trees::LatestJob<ScenerySnapshot, TreeSet> treeJob(buildTreeSet);

    // First frames shouldn't be bare: generate everything within mid range
    // that isn't waiting on a lake tile, and upload it synchronously.
    treeStreamer.prewarm(context.camera.position, treeParams.midDist);
    rockStreamer.prewarm(context.camera.position, rockParams.nearDist);
    updateRockFootprints(rockStreamer.snapshot());
    treeSys.setInstanceSet(
        buildTreeSet({treeStreamer.snapshot(), rockStreamer.snapshot()}));

    // ---- Infinite, camera-following grass --------------------------------
    // The grass compute shader samples ONE heightmap, so "infinite" grass is a
    // heightmap WINDOW that follows the camera: a worker thread re-evaluates
    // the terrain height function around the camera (reusing every texel the
    // previous window already had), and the finished window is uploaded and
    // swapped in with setHeightmap(). Tiles are enumerated around the camera
    // each frame (infiniteField = true), so nothing is tied to the 800 m
    // mountain square any more.
    vkh::grass::WindowParams grassWinParams;
    grassWinParams.res = 512;
    grassWinParams.cell = terrainInfo.lod0CellSize; // == terrain LOD 0 lattice
    grassWinParams.baseHeight = mountainInfo.baseHeight;
    grassWinParams.heightRange =
        mountainInfo.peakHeight - mountainInfo.baseHeight;
    grassWinParams.rockSlopeAngleDeg = mountainInfo.rockSlopeAngleDeg;
    constexpr float kGrassShoreMargin = 1.5f; // no grass in/at the water
    vkh::grass::HeightWindow grassWindow(
        grassWinParams, vkh::mountain::makeHeightFn(mountainInfo),
        [&lakeZones, &rockFootprints](glm::vec2 xz) {
          return lakeZones.blocked(xz, kGrassShoreMargin) ||
                 rockFootprints.blocked(xz);
        },
        [&lakeZones](glm::vec2 a, glm::vec2 b) {
          return lakeZones.tilesReady(a, b);
        },
        [worldConfig](glm::vec2 xz, float h) {
          return worldConfig->sample(xz, h).grassDensity;
        });
    // The window must always cover maxDist + a few tiles around the camera.
    // Half-extent is ~res*cell/2 (~400 m); re-centre once the camera is this
    // far from the window centre, which leaves >250 m of slack for the async
    // build to finish.
    constexpr float kGrassRecenterDist = 128.f;
    // If the window is incomplete (lake data missing) we normally wait and
    // retry, but past this distance we take it rather than run out of window.
    constexpr float kGrassForceAdoptDist = 250.f;
    constexpr float kGrassRetryDelay = 0.5f; // seconds

    auto grassToBinding = [&](const vkh::grass::WindowResult &r) {
      vkh::ImageCreateInfo_data imageInfo{};
      imageInfo.size = glm::uvec2(r.res, r.res);
      imageInfo.data = const_cast<uint32_t *>(r.texels.data());
      imageInfo.layout = vk::ImageLayout::eShaderReadOnlyOptimal;
      imageInfo.usage = vk::ImageUsageFlagBits::eTransferDst |
                        vk::ImageUsageFlagBits::eSampled;
      imageInfo.format = vk::Format::eR16G16Sfloat; // packHalf2x16(y, suit)
      imageInfo.name = "grass window heightmap image";
      vkh::ProceduralGrassSys::HeightmapBinding b;
      b.image = std::make_shared<vkh::Image2D>(context, imageInfo);
      b.worldMin = r.worldMin;
      b.worldSize = r.worldSize;
      b.minY = r.minY;
      b.maxY = r.maxY;
      return b;
    };

    // First window is built synchronously so frame 0 already has grass.
    const glm::vec2 grassStartXZ(context.camera.position.x,
                                 context.camera.position.z);
    glm::vec2 grassWinCenter;
    bool grassWantRetry;
    float grassRetryAt = 0.f;
    std::future<vkh::grass::WindowResult> grassWinFuture; // destroyed (=joined)
                                                          // before grassWindow
    std::optional<vkh::grass::WindowResult> pendingGrassWindow;
    // Kept until this frame slot retires; a transfer must outlive submission.
    std::vector<std::unique_ptr<vkh::Buffer<uint32_t>>> grassUploadStaging(
        context.vulkan.maxFramesInFlight);
    uint64_t grassBuildRockVersion = rockFootprintVersion;
    vkh::ProceduralGrassSys grassSys(
        context, /*patches=*/{}, /*tileSize=*/1.0f, /*bladesPerTileSide=*/16,
        [&] {
          auto r = grassWindow.build(grassStartXZ);
          grassWinCenter = r.worldMin + r.worldSize * 0.5f;
          grassWantRetry = !r.complete;
          return grassToBinding(r);
        }(),
        /*infiniteField=*/true);
    grassSys.flowerChance = 0.06f;

    vkh::wildlife::WolfPacks wolfPacks(
        [&terrainSys](glm::vec2 xz) {
          return terrainSys.sampleHeight(xz, false);
        },
        [&](glm::vec2 xz) {
          // Use known lake masks and rock footprints, plus the manor area.
          return lakeZones.blocked(xz, 2.f) || rockFootprints.blocked(xz) ||
                 (std::abs(xz.x) < 17.f && std::abs(xz.y) < 15.f);
        },
        context.camera.position,
        context.camera.orientation * glm::vec3(0, 0, -1));
    vkh::WolfSys wolfSys(context, entitySys.texturesSetLayout);
    const char *wolfDebugFlag = std::getenv("VULKHAN_WOLF_DEBUG");
    const bool wolfAiDebug = wolfDebugFlag && wolfDebugFlag[0] == '1';
    float nextWolfAiReport = 0;
    if (const char *preview = std::getenv("VULKHAN_PREVIEW_WOLVES");
        preview && preview[0] == '1') {
      wolfPreviewMode = true;
      const auto wolves = wolfPacks.interpolated();
      const glm::vec3 target = wolves[0].position + glm::vec3(0, 1, 0);
      previewPosition = target + glm::vec3(8, 3, -10);
      previewPosition.y =
          std::max(previewPosition.y,
                   terrainInfo.heightFn({previewPosition.x, previewPosition.z},
                                        terrainInfo.lod0CellSize) +
                       2.f);
      previewOrientation = glm::quatLookAt(
          glm::normalize(target - previewPosition), glm::vec3(0, 1, 0));
      context.camera.position = previewPosition;
      context.camera.orientation = previewOrientation;
      vkh::camera::calcViewYXZ(context);
      previewMode = true;
    }

    const int birdCount = 40;

    initBoids(birdCount);

    std::vector<std::vector<size_t>> boidEntityIndices(boids.size());
    for (size_t b = 0; b < boids.size(); ++b) {
      for (size_t m = 0; m < birdScene->meshes.size(); ++m) {
        boidEntityIndices[b].push_back(entities.size());
        entities.emplace_back(vkh::EntitySys::Transform{},
                              vkh::EntitySys::RigidBody{}, birdScene, m);
      }
    }

    // auto piano = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/piano-decent.glb", entitySys.texturesSetLayout);
    // for (size_t i = 0; i < piano->meshes.size(); i++)
    //   entities.emplace_back(
    //       vkh::EntitySys::Transform{.position{10.f, 10.f, 10.f}},
    //       vkh::EntitySys::RigidBody{}, piano, i);
    //
    // auto manorcore = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/manorcore.glb", entitySys.texturesSetLayout);
    // for (size_t i = 0; i < manorcore->meshes.size(); i++)
    //   entities.emplace_back(
    //       vkh::EntitySys::Transform{.position{25.f}, .scale{1.f}},
    //       vkh::EntitySys::RigidBody{}, manorcore, i);
    // // waterSys.downloadDisplacementAtWorldPos();
    //
    // auto shoe = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/MaterialsVariantsShoe.glb",
    //     entitySys.texturesSetLayout);
    // auto playerModel = shoe;
    //
    // std::unordered_map<uint32_t, uint32_t> playersIndices;
    //
    // // Sort to group meshes for indirect drawing
    // std::sort(
    //     entities.begin(), entities.end(),
    //     [](const vkh::EntitySys::Entity &a, const vkh::EntitySys::Entity &b)
    //     {
    //       if (a.scene != b.scene)
    //         return a.scene < b.scene;
    //       return a.meshIndex < b.meshIndex;
    //     });
    //
    // entitySys.updateBuffers();

    // Shared avatar asset; stable handles own every mesh of each player.
    auto shoeScene = entitySys.loadScene("models/MaterialsVariantsShoe.glb");
    std::unordered_map<uint32_t, vkh::EntitySys::Handle> playerEntities;

    StreamLighting streamLighting(context, terrainSys, [&](glm::vec2 p) {
      return lakeZones.blocked(p, 1.f) || rockFootprints.blocked(p);
    });
    GameUI ui(context);

    ui.setGraphicsSettingsHandler([&](const vkh::Settings &settings) {
      grassSys.maxDist = static_cast<float>(settings.grassDistance);
      terrainSys.setShadowSteps(settings.terrainShadowSteps);
      postProcessingSys.setEnabled(settings.postEffects);
    });

    // Live sky-resolution changes: the settings menu's sky resolution
    // button (see UI.cpp) persists the new value to settings.bin and then
    // fires this handler so the change is visible immediately rather than
    // only on next launch. Capturing skySys by reference is safe here --
    // this handler doesn't outlive skySys, both are local to this scope.
    ui.setSkyResolutionHandler(
        [&skySys](uint32_t milkyWayFaceSize, uint32_t discTurbulenceSize) {
          skySys.rebake(milkyWayFaceSize, discTurbulenceSize);
        });

    bool updateParticleSysAttractor = false;
    std::vector<glm::mat4> newTransform = genTransform();
    std::vector<glm::mat4> prevTransform = newTransform;
    float timeOfNewTransform = 0.f;
    float nextParticlePatchUpdate = 0.f;
    glm::ivec2 lastParticleCell{INT_MAX};

    // These two handlers reference state that's local to run() (the debug
    // particle-attractor regen and the audio fade timer), so they're wired
    // up here rather than inside GameUI.
    ui.addWorldViewKeyHandler([&](int key, int scancode, int action, int mods) {
      if (key == GLFW_KEY_R &&
          (action == GLFW_PRESS || action == GLFW_REPEAT)) {
        updateParticleSysAttractor = true;
        timeOfNewTransform = context.time;
        return true;
      }
      if (key == GLFW_KEY_G && action == GLFW_PRESS) {
        for (auto &mat : newTransform) {
          std::println("{}", glm::to_string(mat));
        }
      }
      return false;
    });

    ui.addWorldViewFocusHandler([&](int focused) {
      audioFadeBegin = std::chrono::high_resolution_clock::now();
      return false;
    });

    vkh::GpuProfiler gpuProfiler(context);
    auto currentTime = std::chrono::high_resolution_clock::now();
    auto initTime = currentTime;
    vkh::EntitySys::Entity *lastPicked = nullptr;
    while (!glfwWindowShouldClose(context.window)) {
      vkh::CpuScope frameCpuScope("frame");
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();

      context.time = std::chrono::duration<float>(newTime - initTime).count();

      ui.update(context, frameTime);

      // featherDuckGuard.update();

      float deltaTime =
          std::chrono::duration<float>(newTime - audioFadeBegin).count();
      float volume;
      if (context.window.isFocused()) {
        volume = glm::clamp(deltaTime * audioFadeSpeed, 0.f, 1.f);
      } else {
        volume = glm::clamp(1.f - deltaTime * audioFadeSpeed, 0.f, 1.f);
      }
      vkh::audio::setVolume(volume);

      static bool dontDoOnce = true;
      // if (!dontDoOnce) {
      //   if (!context.window.isFocused())
      //     continue;
      // }
      dontDoOnce = false;

      currentTime = newTime;

      // Disable collisions with entities by providing empty array
      std::vector<vkh::EntitySys::Entity> empty;
      vkh::input::update(context, empty);

      // Entity picking visualization
      // {
      //   auto pointed = entitySys.getPointingAt(1.0f);
      //   if (pointed != lastPicked) {
      //     if (lastPicked) {
      //       lastPicked->color = glm::vec4(1.0f); // Reset
      //     }
      //
      //     lastPicked = pointed;
      //
      //     if (lastPicked) {
      //       lastPicked->color =
      //           glm::vec4(2.0f, 0.5f, 0.5f, 1.0f); // Highlight Red-ish
      //     }
      //   }
      // }

      // Networking: pull in whatever arrived since last frame, then push
      // our own transform out. Cheap no-op until the Hello handshake has
      // completed (see NetworkSession::sendUpdate).
      netSession.poll();
      netSession.sendUpdate(context.camera.position,
                            glm::quat_cast(context.camera.viewMatrix));

      // Spawn/despawn whole models without mesh loops or index repair.
      {
        const auto &remotePlayers = netSession.players();
        for (auto it = playerEntities.begin(); it != playerEntities.end();) {
          if (remotePlayers.contains(it->first)) {
            ++it;
            continue;
          }
          entitySys.destroy(it->second);
          it = playerEntities.erase(it);
        }
        for (const auto &[serverId, remote] : remotePlayers) {
          auto [it, inserted] = playerEntities.try_emplace(serverId);
          if (inserted) {
            it->second = entitySys.spawn(shoeScene);
            entitySys.setId(it->second, serverId);
          }
          vkh::EntitySys::Transform transform;
          transform.position = remote.position;
          transform.orientation = remote.orientation;
          entitySys.setTransform(it->second, transform);
        }
      }

      vkh::audio::update(context);
      if (wolfPreviewMode) {
        const auto wolves = wolfPacks.interpolated();
        auto leader =
            std::find_if(wolves.begin(), wolves.end(),
                         [](const auto &wolf) { return wolf.active; });
        if (leader != wolves.end()) {
          const glm::vec3 target = leader->position + glm::vec3(0, 1.3f, 0);
          previewPosition = target + glm::vec3(10, 4, -12);
          previewPosition.y = std::max(
              previewPosition.y,
              terrainSys.sampleHeight({previewPosition.x, previewPosition.z}) +
                  2.f);
          previewOrientation = glm::quatLookAt(
              glm::normalize(target - previewPosition), glm::vec3(0, 1, 0));
        }
      }
      if (previewMode) {
        context.camera.position = previewPosition;
        context.camera.orientation = previewOrientation;
      }
      context.camera.projectionMatrix = glm::perspective(
          glm::radians(float(vkh::settings::current().fieldOfView)),
          context.window.aspectRatio, context.camera.far, context.camera.near);
      context.camera.projectionMatrix[1][1] *= -1.f; // Flip Y
      vkh::camera::calcViewYXZ(context);

      // Harvest CPU grass windows without waiting. GPU upload is recorded
      // below, after beginFrame has retired the frame slot's previous use.
      if (ui.isWorldViewActive()) {
        const glm::vec2 camXZ(context.camera.position.x,
                              context.camera.position.z);
        const float distFromWin = glm::distance(camXZ, grassWinCenter);
        if (grassWinFuture.valid()) {
          if (grassWinFuture.wait_for(std::chrono::seconds(0)) ==
              std::future_status::ready) {
            auto r = grassWinFuture.get();
            if (r.complete || distFromWin > kGrassForceAdoptDist) {
              grassWinCenter = r.worldMin + r.worldSize * 0.5f;
              grassWantRetry =
                  !r.complete || grassBuildRockVersion != rockFootprintVersion;
              pendingGrassWindow = std::move(r);
            } else {
              // Lake tiles still being analysed: keep the current window
              // (it still covers the camera) and try again shortly.
              grassWantRetry = true;
            }
            grassRetryAt = context.time + kGrassRetryDelay;
          }
        } else if (distFromWin > kGrassRecenterDist ||
                   (grassWantRetry && context.time >= grassRetryAt)) {
          grassBuildRockVersion = rockFootprintVersion;
          grassWinFuture =
              std::async(std::launch::async, [&grassWindow, camXZ] {
                return grassWindow.build(camXZ);
              });
        }
      }

      if (auto commandBuffer = vkh::profileCpu("frame acquire/fence", [&] {
            return vkh::renderer::beginFrame(context);
          })) {
        int frameIndex = vkh::renderer::getFrameIndex();
        gpuProfiler.begin(commandBuffer, frameIndex);
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
        };

        if (ui.isWorldViewActive())
          vkh::profileCpu("environment emitters",
                          [&] { streamLighting.update(); });
        vkh::GlobalUbo ubo{};
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.resolution = context.window.size;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = context.time;
        vkh::applyEnvironment(ubo, context.environment);
        context.vulkan.globalUBOs[frameIndex].write(&ubo);
        context.vulkan.globalUBOs[frameIndex].flush();

        if (ui.isWorldViewActive()) {
          if (pendingGrassWindow) {
            vkh::CpuScope uploadScope("grass window upload");
            const auto &r = *pendingGrassWindow;
            vkh::ImageCreateInfo_empty imageInfo{};
            imageInfo.size = glm::uvec2(r.res, r.res);
            imageInfo.format = vk::Format::eR16G16Sfloat;
            imageInfo.name = "streamed grass window heightmap";
            auto image = std::make_shared<vkh::Image2D>(context, imageInfo);
            auto &stage = grassUploadStaging[frameIndex];
            if (!stage ||
                stage->getSize() < r.texels.size() * sizeof(uint32_t)) {
              stage = std::make_unique<vkh::Buffer<uint32_t>>(
                  context, vk::BufferUsageFlagBits::eTransferSrc,
                  vk::MemoryPropertyFlagBits::eHostVisible |
                      vk::MemoryPropertyFlagBits::eHostCoherent,
                  r.texels.size());
              stage->map();
            }
            stage->write(r.texels.data(), r.texels.size() * sizeof(uint32_t));
            image->recordTransitionLayout(commandBuffer,
                                          vk::ImageLayout::eTransferDstOptimal);
            image->recordCopyFromBuffer(commandBuffer, *stage);
            image->recordTransitionLayout(
                commandBuffer, vk::ImageLayout::eShaderReadOnlyOptimal);
            grassSys.setHeightmap(
                {image, r.worldMin, r.worldSize, r.minY, r.maxY});
            pendingGrassWindow.reset();
          }
          if (updateParticleSysAttractor) {
            prevTransform = newTransform;
            newTransform = genTransform();
            updateParticleSysAttractor = false;
            timeOfNewTransform = context.time;
          }
          std::vector<glm::mat4> blendedTransform(newTransform.size());
          for (size_t i = 0; i < newTransform.size(); i++) {
            if (i > prevTransform.size() - 1) {
              blendedTransform[i] = newTransform[i];
              continue;
            }
            float p =
                glm::clamp((context.time - timeOfNewTransform) * .5f, 0.f, 1.f);
            blendedTransform[i] =
                glm::mix(prevTransform[i], newTransform[i], p);
          }
          particleSys.setAttractor(blendedTransform);
          const glm::ivec2 particleCell = glm::ivec2(glm::floor(
              glm::vec2{context.camera.position.x, context.camera.position.z} /
              8.f));
          if (context.time >= nextParticlePatchUpdate ||
              particleCell != lastParticleCell) {
            particleSys.setEnvironment(vkh::particlePatches(
                context.camera.position,
                [&](glm::vec2 xz) {
                  return terrainSys.sampleHeight(xz, false);
                },
                [&](glm::vec2 xz) {
                  return lakeZones.blocked(xz, 1.f) ||
                         rockFootprints.blocked(xz);
                }));
            nextParticlePatchUpdate = context.time + .5f;
            lastParticleCell = particleCell;
          }

          updateBoids(frameTime);
          for (size_t b = 0; b < boids.size(); ++b) {
            const Boid &boid = boids[b];

            glm::quat orientation{1.f, 0.f, 0.f, 0.f};
            if (glm::length(boid.vel) > 1e-4f) {
              glm::vec3 forward = glm::normalize(boid.vel);
              glm::quat newOrientation =
                  glm::rotation(glm::vec3(0.f, 0.f, -1.f), forward);

              for (size_t entityIdx : boidEntityIndices[b]) {
                auto &orientation = entities[entityIdx].transform.orientation;
                const float turnRate = 3.f;
                orientation = glm::slerp(orientation, newOrientation,
                                         glm::min(frameTime * turnRate, 1.f));
              }
            }
            for (size_t entityIdx : boidEntityIndices[b]) {
              entities[entityIdx].transform.position = boid.pos;
            }
          }

          // Forest streaming: tiles generate on workers, the instance-data
          // rebuild runs on a worker too; only the pointer swap happens here.
          const bool forestChanged =
              treeStreamer.update(context.camera.position);
          const bool rocksChanged =
              rockStreamer.update(context.camera.position);
          if (rocksChanged) {
            updateRockFootprints(rockStreamer.snapshot());
            grassWantRetry = true;
          }
          if (forestChanged || rocksChanged)
            treeJob.request({treeStreamer.snapshot(), rockStreamer.snapshot()});
          {
            TreeSet builtTrees;
            if (treeJob.poll(builtTrees))
              treeSys.setInstanceSet(std::move(builtTrees));
          }
          treeSys.updateBuffers();
          particleSys.update();
          entitySys.prepareFrame();

          vkh::profileCpu("grass update", [&] { grassSys.updateBuffers(); });
          grassSys.cull(commandBuffer);

          // Recorded into this frame's command buffer, fixed 60 Hz timestep
          // (see SmokeSys::update).
          smokeSys.update(commandBuffer, frameTime);
          // Streams terrain chunks; must be outside any render pass.
          vkh::profileCpu("terrain streaming",
                          [&] { terrainSys.update(commandBuffer); });
          vkh::profileCpu("wolf AI", [&] {
            wolfPacks.update(frameTime, context.camera.position,
                             context.camera.orientation * glm::vec3(0, 0, -1),
                             !wolfPreviewMode);
          });
          wolfSys.update(wolfPacks.interpolated());
          if (wolfAiDebug && context.time >= nextWolfAiReport) {
            const auto status = wolfPacks.status();
            for (size_t pack = 0; pack < status.size(); ++pack)
              std::println(
                  "[wolf AI] pack={} state={} stamina={:.2f} "
                  "threat={:.2f} route={} plans={}",
                  pack, vkh::wildlife::WolfPacks::stateName(status[pack].state),
                  status[pack].stamina, status[pack].threat,
                  status[pack].routePoints, status[pack].routePlans);
            nextWolfAiReport = context.time + 5.f;
          }
          vkh::profileCpu("lake streaming",
                          [&] { lakes.update(context.camera.position, 1); });
        }

        renderFrame(context,
                    {ui, terrainSys, entitySys, wolfSys, treeSys, grassSys,
                     skySys, particleSys, postProcessingSys, gpuProfiler},
                    lakes);
      }
    }
    vkDeviceWaitIdle(context.vulkan.device);
  }
  vkh::renderer::cleanup(context);
  vkh::cleanup(context);
  cleanupWindow(context);
  vkh::audio::cleanup();
}

} // namespace app
