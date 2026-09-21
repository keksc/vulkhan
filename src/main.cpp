#include <GLFW/glfw3.h>
#include <glm/gtc/quaternion.hpp>
#include <glm/gtx/string_cast.hpp>

#include "UI.hpp"
#include "boids.hpp"
#include "modding.hpp"
#include "mountain.hpp"
#include "settings.hpp"
#include "vkh/audio.hpp"
#include "vkh/camera.hpp"
#include "vkh/cleanup.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/init.hpp"
#include "vkh/input.hpp"
#include "vkh/paths.hpp"
#include "vkh/renderer.hpp"
#include "vkh/sceneBuilder.hpp"
#include "vkh/scene.hpp"
#include "vkh/swapChain.hpp"
#include "vkh/systems/entity/entities.hpp"
#include "vkh/systems/grass/proceduralGrass.hpp"
#include "vkh/systems/tree/treeSys.hpp"
#include "vkh/systems/particles.hpp"
#include "vkh/systems/planarReflectionSys.hpp"
#include "vkh/systems/postProcessing.hpp"
#include "vkh/systems/sky.hpp"
#include "vkh/systems/smoke/compositeEffect.hpp"
#include "vkh/systems/smoke/smoke.hpp"
#include "vkh/systems/water/water.hpp"
#include "vkh/window.hpp"

#include "dungeonGenerator.hpp"
#include "network/networkSession.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <print>
#include <random>
#include <string>
#include <unordered_map>

std::mt19937 rng{std::random_device{}()};

// Extracts the 6 view-frustum planes (as ax+by+cz+d, normalized so (a,b,c)
// is a unit normal pointing INTO the frustum) from a combined proj*view
// matrix. Written against Vulkan's [0,1] NDC depth range (not OpenGL's
// [-1,1]) - context.camera.projectionMatrix already comes out of
// glm::perspective with the [1][1] *= -1 Y-flip applied elsewhere in this
// file, which only negates a row and doesn't change the validity of this
// row-combination approach.
std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &m) {
  // glm matrices are column-major (m[col][row]) - build each "row" of the
  // matrix as it would be addressed in the standard row-major Gribb/
  // Hartmann derivation.
  glm::vec4 row0{m[0][0], m[1][0], m[2][0], m[3][0]};
  glm::vec4 row1{m[0][1], m[1][1], m[2][1], m[3][1]};
  glm::vec4 row2{m[0][2], m[1][2], m[2][2], m[3][2]};
  glm::vec4 row3{m[0][3], m[1][3], m[2][3], m[3][3]};

  std::array<glm::vec4, 6> planes = {
      row3 + row0, // left:   w + x >= 0
      row3 - row0, // right:  w - x >= 0
      row3 + row1, // bottom: w + y >= 0
      row3 - row1, // top:    w - y >= 0
      row2,        // near:       z >= 0   (Vulkan z in [0, w], not [-w, w])
      row3 - row2, // far:    w - z >= 0
  };
  for (auto &p : planes) {
    float len = glm::length(glm::vec3(p));
    if (len > 1e-8f)
      p /= len;
  }
  return planes;
}

// Conservative sphere-vs-frustum test: true if any part of the sphere
// could be inside. Used to decide whether a lake's reflection is worth
// rendering this frame at all, before paying for a full extra opaque-
// scene pass for it (see PlanarReflectionSys's per-lake cost note).
bool sphereInFrustum(const std::array<glm::vec4, 6> &planes,
                     const glm::vec3 &center, float radius) {
  for (const auto &p : planes) {
    float distance = glm::dot(glm::vec3(p), center) + p.w;
    if (distance < -radius)
      return false;
  }
  return true;
}

std::vector<glm::mat4> genTransform() {

  std::uniform_int_distribution<uint32_t> countDist(
      3, 5); // 3 to 5 transformations looks great in 3D
  uint32_t numTransforms = countDist(rng);

  // Keep scales strictly bound so the 3D volume remains stable and
  // contained
  std::uniform_real_distribution<float> scaleDist(0.35f, 0.55f);
  std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
  std::uniform_real_distribution<float> transDist(
      -0.6f, 0.6f); // 3D translation limits

  std::vector<glm::mat4> transformationMats;
  transformationMats.reserve(numTransforms);

  for (uint32_t i = 0; i < numTransforms; ++i) {
    float sX = scaleDist(rng);
    float sY = scaleDist(rng);
    float sZ = scaleDist(rng); // Add Z scaling

    // Random angles for all 3 axes
    float ax = angleDist(rng);
    float ay = angleDist(rng);
    float az = angleDist(rng);

    // Create distinct rotation matrices for each axis
    glm::mat4 rotX =
        glm::rotate(glm::mat4(1.0f), ax, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rotY =
        glm::rotate(glm::mat4(1.0f), ay, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotZ =
        glm::rotate(glm::mat4(1.0f), az, glm::vec3(0.0f, 0.0f, 1.0f));

    // Combine rotations: R = Z * Y * X
    glm::mat4 combinedRotation = rotZ * rotY * rotX;

    // Apply scaling directly to the directional basis vectors
    // (columns 0, 1, 2)
    combinedRotation[0] = combinedRotation[0] * sX;
    combinedRotation[1] = combinedRotation[1] * sY;
    combinedRotation[2] = combinedRotation[2] * sZ;

    // Translation
    combinedRotation[3][0] = transDist(rng);
    combinedRotation[3][1] = transDist(rng);
    combinedRotation[3][2] = transDist(rng);

    combinedRotation[3][3] = 1.0f;

    transformationMats.push_back(combinedRotation);
  }
  return transformationMats;
}

void run() {
  paths::initCacheDir("vulkhan");
  paths::setWorkingDirectoryToExecutable();
  vkh::settings::load();
  vkh::settings::validateSkyResolution();

  vkh::EngineContext context{};
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
    // vkh::TreeSys treeSys(context);

    vkh::SmokeSys smokeSys(context);

    vkh::WSTessendorf waveSim(context);
    // windStrength is the engine's abstract 0-1 wind "strength" knob (see
    // WSTessendorf.hpp/water.hpp - it's a shading-effect input, not a
    // physical speed itself; WSTessendorf.cpp maps it onto an actual m/s
    // value for the JONSWAP spectrum). This was set to 10.f, ten times
    // past that documented range - glm::mix() doesn't clamp its `t`, so
    // that extrapolated wind speed way past the intended 0.5-20 m/s band,
    // which blows up the spectrum (and with it wave amplitude) far beyond
    // anything WaterCreateInfo::waveScale tuning downstream expects to be
    // damping. 1.f is full strength within the actual intended range.
    waveSim.windStrength = 1.f;
    waveSim.windDir = glm::normalize(glm::vec2{1.f, 0.3f});
    waveSim.warmUp();

    vkh::ParticleSys particleSys(context);

    // Smoke is currently the only postprocessing stage - SSR has been
    // removed (see ssr.comp/ssrEffect.hpp, no longer used). Add further
    // stages here as additional makeComputePassFactory<...>(...) entries;
    // PostProcessingSys itself doesn't need to change for that.
    std::vector<vkh::ComputePassFactory> postEffects;
    postEffects.push_back(vkh::makeComputePassFactory<vkh::SmokeCompositePush>(
        [&smokeSys](vkh::EngineContext &ctx) {
          return std::make_unique<vkh::SmokeCompositeEffect>(ctx, smokeSys);
        }));
    vkh::PostProcessingSys postProcessingSys(context, std::move(postEffects));

    auto &entities = entitySys.entities;
    // generateDungeon(context, entitySys);
    // auto houseScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
    //     context, "models/haunted_victorian_house.glb",
    //     entitySys.texturesSetLayout);
    // houseScene->uploadModelGPU(entitySys.texturesSetLayout);
    // for (size_t i = 0; i < houseScene->meshes.size(); i++)
    //   entities.emplace_back(vkh::EntitySys::Transform{},
    //                         vkh::EntitySys::RigidBody{}, houseScene, i);

    auto birdScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
        context, "models/paperplane.glb");
    birdScene->uploadModelGPU(entitySys.texturesSetLayout);

    vkh::mountain::MountainCreateInfo mountainInfo{};
    mountainInfo.treeDensity = 0.1f;
    mountainInfo.resolution = glm::uvec2{512u};
    mountainInfo.worldSize = glm::vec2{800.f};
    mountainInfo.baseHeight = 0.f;
    mountainInfo.peakHeight = 120.f;
    mountainInfo.octaves = 5;
    mountainInfo.erosionScale = 30.f;
    mountainInfo.gullyWeight = 0.5f;
    mountainInfo.erosionStrength = .22f;

    vkh::mountain::MountainHeightmap mountainHeightmap;
    std::vector<vkh::mountain::Lake> mountainLakes;
    std::vector<vkh::mountain::TreePlacement> treePlacements;
    auto mountainScene = vkh::mountain::generate(
        context, entitySys.texturesSetLayout, mountainInfo, &mountainHeightmap,
        &mountainLakes, &treePlacements);
    for (size_t i = 0; i < mountainScene->meshes.size(); i++)
      entities.emplace_back(vkh::EntitySys::Transform{},
                            vkh::EntitySys::RigidBody{}, mountainScene, i);

    // auto treeScene = std::make_shared<vkh::Scene<vkh::TreeSys::Vertex>>(
    //     context, "models/fir.glb");
    // treeScene->lodDistances = {30.f, 80.f}; // closer than 30 -> LOD0, 30-80 -> LOD1, beyond -> LOD2
    // treeScene->uploadModelGPU(treeSys.texturesSetLayout);

    // Mesh 0 = trunk/branches (opaque wood), mesh 1 = needle/leaf cards
    // (foliage: two-sided + back-transmission + alpha cutout - see
    // TreeSys::TreeVariant's doc comment). Adjust this list if fir.glb's
    // actual mesh order/count differs - each entry corresponds 1:1 with
    // treeScene->meshes[i].
    // uint32_t firVariant =
    //     treeSys.addVariant({treeScene, {false, true}});
    //
    // std::vector<vkh::TreeSys::TreePlacement> treeSysPlacements;
    // treeSysPlacements.reserve(treePlacements.size());
    // for (const auto &tp : treePlacements)
    //   treeSysPlacements.push_back(
    //       {tp.position, tp.yaw, tp.scale, firVariant});
    // treeSys.setPlacements(std::move(treeSysPlacements));

    std::vector<std::unique_ptr<vkh::PlanarReflectionSys>> lakePlanarRefls;
    lakePlanarRefls.reserve(mountainLakes.size());

    std::vector<std::unique_ptr<vkh::WaterSys>> lakeWaters;
    lakeWaters.reserve(mountainLakes.size());
    for (const auto &lake : mountainLakes) {
      vkh::WaterCreateInfo lakeInfo{};
      lakeInfo.center = lake.worldCenter;
      lakeInfo.size = lake.worldSize;
      const float nativeWaveSpacing =
          vkh::WSTessendorf::tileLength / vkh::WSTessendorf::tileSize;
      const float cellWorldWidth =
          std::min(lake.worldSize.x, lake.worldSize.y) /
          static_cast<float>(vkh::WaterSys::tileResolution);
      const float smallLakeDamping =
          std::clamp(cellWorldWidth / nativeWaveSpacing, 0.15f, 1.0f);
      // Was 14.f - about 175x WaterCreateInfo::waveScale's own documented
      // default (0.08f, see water.hpp). vertexUBO.scale multiplies the raw
      // FFT height field directly in water.tese, so that wasn't a subtle
      // tuning knob - it blew displaced vertex height up to the point of
      // turning the mesh into huge radiating spikes rather than a
      // wave-covered surface. 0.08f matches the struct's own default;
      // still scaled by smallLakeDamping like chopScale below, so small
      // lakes stay proportionally calmer.
      lakeInfo.waveScale = 0.008f * smallLakeDamping;
      // Was 0.2f - close to WaterCreateInfo::chopScale's own header
      // default (0.3f), but that default predates this engine's per-lake
      // tessellation going up to 24 subdivisions per base cell at close
      // range (see water.tesc) - at that vertex density, world-space
      // spacing between adjacent TESSELLATED vertices is small enough
      // that even this "moderate" a chop displacement regularly exceeds
      // it, so neighboring vertices overtake each other in XZ and the
      // mesh folds over itself (see water.tese's own comment on
      // chopScale). That's the self-intersecting, "curling" crests -
      // it's chop steepness overdriving the grid, not a height/waveScale
      // problem. 0.05f keeps horizontal chop-driven sharpening
      // comfortably under that fold-over threshold at the tessellation
      // levels this engine actually reaches.
      lakeInfo.chopScale = 0.005f * smallLakeDamping;
      lakeInfo.animSpeed = 1.f;
      lakeInfo.uvRepeat = lake.worldSize / 15.f;
      lakeInfo.cellMask = lake.cellMask;
      lakeInfo.maskResolution = lake.maskResolution;
      lakeWaters.push_back(
          std::make_unique<vkh::WaterSys>(context, skySys, waveSim, lakeInfo));

      auto &planarRefl = lakePlanarRefls.emplace_back(
          std::make_unique<vkh::PlanarReflectionSys>(context));
      lakeWaters.back()->setPlanarReflectionSource(planarRefl.get());
    }

    std::vector<vkh::ProceduralGrassSys::GrassPatch> grassPatches = {
        {glm::vec2(0.0f, 0.0f), mountainHeightmap.worldSize},
    };
    vkh::ProceduralGrassSys::HeightmapBinding grassHeightmap{
        mountainHeightmap.image, mountainHeightmap.worldMin,
        mountainHeightmap.worldSize};
    vkh::ProceduralGrassSys grassSys(context, grassPatches, /*tileSize=*/1.0f,
                                     /*bladesPerTileSide=*/8, grassHeightmap);
    grassSys.windStrength = 1.f;
    grassSys.windDir = glm::normalize(glm::vec2(1.0f, 0.4f));
    grassSys.flowerChance = 0.06f;

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

    // Remote-player avatars. Loaded once; per-player entities are spawned
    // and despawned each frame by diffing netSession.players() against
    // playerEntityIndices (see the networking block in the main loop
    // below). Always append these *after* every other fixed-index consumer
    // (boidEntityIndices, in particular) -- despawns use swap-and-pop,
    // which only ever relocates other player entities, never anything
    // earlier in the vector.
    auto shoeScene = std::make_shared<vkh::Scene<vkh::EntitySys::Vertex>>(
        context, "models/MaterialsVariantsShoe.glb");
    shoeScene->uploadModelGPU(entitySys.texturesSetLayout);

    // server session id -> indices (one per shoeScene mesh) into `entities`
    // for that player's avatar.
    std::unordered_map<uint32_t, std::vector<size_t>> playerEntityIndices;

    GameUI ui(context);

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

    auto currentTime = std::chrono::high_resolution_clock::now();
    auto initTime = currentTime;
    vkh::EntitySys::Entity *lastPicked = nullptr;
    while (!glfwWindowShouldClose(context.window)) {
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

      // Sync remote player avatars against netSession.players(): spawn a
      // shoe-model entity group for anyone newly present, update transforms
      // for everyone still connected, and despawn anyone netSession has
      // already erased (i.e. who sent a Leave).
      {
        const auto &remotePlayers = netSession.players();

        for (auto it = playerEntityIndices.begin();
             it != playerEntityIndices.end();) {
          if (remotePlayers.contains(it->first)) {
            ++it;
            continue;
          }

          // Swap-and-pop each of this player's entities. This only ever
          // relocates *other* player entities (looked up by id below,
          // never by a held index), so boidEntityIndices' fixed indices --
          // all sitting earlier in the vector -- are never disturbed.
          std::vector<size_t> indices = it->second;
          std::sort(indices.begin(), indices.end(), std::greater<>());
          for (size_t idx : indices) {
            size_t lastIdx = entities.size() - 1;
            if (idx != lastIdx) {
              entities[idx] = std::move(entities[lastIdx]);
              uint32_t movedId = entities[idx].id;
              if (movedId != vkh::EntitySys::Entity::LOCAL_ENTITY_ID) {
                auto movedIt = playerEntityIndices.find(movedId);
                if (movedIt != playerEntityIndices.end()) {
                  for (auto &storedIdx : movedIt->second) {
                    if (storedIdx == lastIdx) {
                      storedIdx = idx;
                      break;
                    }
                  }
                }
              }
            }
            entities.pop_back();
          }

          it = playerEntityIndices.erase(it);
          entitySys.markStructuralDirty();
        }

        for (const auto &[serverId, remote] : remotePlayers) {
          auto found = playerEntityIndices.find(serverId);
          if (found == playerEntityIndices.end()) {
            std::vector<size_t> newIndices;
            newIndices.reserve(shoeScene->meshes.size());
            for (size_t m = 0; m < shoeScene->meshes.size(); ++m) {
              newIndices.push_back(entities.size());
              entities.emplace_back(vkh::EntitySys::Transform{},
                                    vkh::EntitySys::RigidBody{}, shoeScene, m);
              entities.back().id = serverId;
            }
            found = playerEntityIndices.emplace(serverId, std::move(newIndices))
                        .first;
            entitySys.markStructuralDirty();
          }

          for (size_t idx : found->second) {
            entities[idx].transform.position = remote.position;
            entities[idx].transform.orientation = remote.orientation;
          }
        }
      }

      vkh::audio::update(context);
      context.camera.projectionMatrix = glm::perspective(
          1.919'862'177f /*human FOV*/, context.window.aspectRatio,
          context.camera.far, context.camera.near);
      context.camera.projectionMatrix[1][1] *= -1.f; // Flip Y
      vkh::camera::calcViewYXZ(context);

      if (auto commandBuffer = vkh::renderer::beginFrame(context)) {
        int frameIndex = vkh::renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
        };

        vkh::GlobalUbo ubo{};
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.resolution = context.window.size;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = context.time;
        context.vulkan.globalUBOs[frameIndex].write(&ubo);
        context.vulkan.globalUBOs[frameIndex].flush();

        if (ui.isWorldViewActive()) {
          if (updateParticleSysAttractor) {
            prevTransform = newTransform;
            newTransform = genTransform();
            updateParticleSysAttractor = false;
          }
          std::vector<glm::mat4> blendedTransform(newTransform.size());
          for (size_t i = 0; i < newTransform.size(); i++) {
            if (i > prevTransform.size() - 1) {
              blendedTransform[i] = glm::mat4{0.f};
              continue;
            }
            float p = (context.time - timeOfNewTransform) * .5f;
            blendedTransform[i] =
                glm::mix(prevTransform[i], newTransform[i], p);
          }
          particleSys.setAttractor(blendedTransform);

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

          entitySys.updateBuffers();
          // treeSys.updateBuffers();
          particleSys.update();
          entitySys.updateJoints();

          grassSys.updateBuffers();
          grassSys.cull(commandBuffer);

          smokeSys.update();
        }

        // Planar reflections: one pass per lake, into that lake's own
        // offscreen target, strictly before beginMsaaPass() below - see
        // PlanarReflectionSys::updateReflectionCamera()'s doc comment on
        // why (Vulkan doesn't allow starting a new rendering scope while
        // already inside another one). Only the opaque scene (sky +
        // entities, NOT water itself - no reflection-of-a-reflection)
        // goes into each reflection target.
        //
        // Skipped for any lake outside the REAL camera's frustum this
        // frame - if the water surface itself isn't on screen, nothing
        // will ever sample its reflection texture, so the extra
        // opaque-scene render would be pure waste. Note this is a
        // necessarily coarse test (a bounding sphere over the lake's
        // whole footprint, not the actual displaced water mesh). Forced
        // through anyway on any frame-in-flight slot that has never
        // rendered yet - see PlanarReflectionSys::
        // hasEverRenderedCurrentSlot()'s doc comment for why (each slot's
        // image starts in eUndefined and only WaterSys's own descriptor
        // claims eShaderReadOnlyOptimal regardless of whether that's
        // actually true yet).
        //
        // Each rendered lake's entities are re-culled against ITS
        // mirrored camera first (EntitySys::cullForViewProj()) - fixing
        // the previously-documented gap where reflection content was
        // limited to whatever the real camera's frustum last culled.
        // Because there's only one set of compacted/indirect-draw
        // buffers per frame-in-flight (see cullForViewProj()'s doc
        // comment), the REAL camera's cull(commandBuffer) call has to
        // happen AFTER every lake's reflection pass here, not before -
        // that's why it no longer sits up in the entitySys.updateBuffers()
        // block above.
        if (ui.isWorldViewActive()) {
          const glm::mat4 camViewProj =
              context.camera.projectionMatrix * context.camera.viewMatrix;
          const std::array<glm::vec4, 6> camFrustum =
              extractFrustumPlanes(camViewProj);

          for (size_t i = 0; i < mountainLakes.size(); ++i) {
            const auto &lake = mountainLakes[i];
            const float boundingRadius = glm::length(lake.worldSize) * 0.5f;
            bool visible =
                sphereInFrustum(camFrustum, lake.worldCenter, boundingRadius);

            auto &planarRefl = *lakePlanarRefls[i];
            if (!visible && planarRefl.hasEverRenderedCurrentSlot())
              continue;

            planarRefl.updateReflectionCamera(lake.worldCenter.y);
            entitySys.cullForViewProj(commandBuffer,
                                      planarRefl.getReflViewProj());
            // treeSys.cullForViewProj(commandBuffer,
                                    // planarRefl.getReflViewProj());
            planarRefl.recordReflectionPass(
                commandBuffer, [&](vk::CommandBuffer reflCmd) {
                  vk::DescriptorSet reflSet =
                      planarRefl.getReflectionGlobalSet();
                  skySys.render(reflSet);
                  entitySys.render(reflSet, planarRefl.getClipPlane());
                  // treeSys.render(reflSet, planarRefl.getClipPlane());
                });
          }

          // Restore the real camera's culled set - see the comment above
          // on why this must run after every reflection pass this frame,
          // not before. Cheap even when no lake was rendered above (a
          // frustum culled during the reflection loop replaces what
          // this same call would otherwise have produced earlier).
          entitySys.cull(commandBuffer);
          // treeSys.cull(commandBuffer);

          // lakeWaters[i]->update() reads getPlanarReflectionViewProj()
          // and writes it (plus the reflection texture binding) into
          // THIS frame's uniform buffer/descriptor set - it has to run
          // AFTER the reflection loop above, not before, or it would
          // read the mirrored camera matrix from the PREVIOUS frame
          // while binding the texture that loop just rendered THIS
          // frame. That one-frame mismatch is exactly what caused the
          // reflection to visibly "swim"/misalign relative to the water
          // surface as the camera moved - the UV math and the sampled
          // image were always one frame out of sync with each other,
          // even though each individually was internally consistent.
          //
          // For a lake this frame's loop SKIPPED (outside the real
          // camera's frustum and already rendered before - see
          // hasEverRenderedCurrentSlot()), getPlanarReflectionViewProj()
          // correctly returns THAT lake's last-actually-rendered matrix,
          // matching its last-actually-rendered (now stale, but still
          // internally consistent) texture - so a distant/rarely-visible
          // lake's reflection may lag the camera by several frames
          // (intentional, see the reflection loop's culling comment
          // above), but won't be internally distorted/mismatched the way
          // the swimming case was.
          //
          // The first lake drives waveSim's simulation step this frame;
          // every other lake just re-samples the resulting texture, so
          // pass driveSimulation = false for them. This is a compute
          // dispatch (modelTess.recordComputeWaves()) - safe to record
          // here since nothing between here and beginMsaaPass below has
          // an active vkCmdBeginRendering scope open (each lake's
          // recordReflectionPass() call above already closed its own).
          for (size_t i = 0; i < lakeWaters.size(); ++i)
            lakeWaters[i]->update(/*driveSimulation=*/i == 0);
        }

        // --- Pass 0: MSAA render, resolving into scene color + resolved
        // depth (replaces beginSwapChainRenderPass + subpass 0) ---
        vkh::renderer::beginMsaaPass(context, commandBuffer);

        if (ui.isWorldViewActive()) {
          skySys.render();
          entitySys.render();
          // treeSys.render();
          grassSys.render();
          for (auto &lakeWater : lakeWaters)
            lakeWater->render();
        }

        // --- Transition to pass 1: 1x direct render onto resolved buffers
        // (replaces vkCmdNextSubpass) ---
        vkh::renderer::transitionToOneXPass(context, commandBuffer);

        if (ui.isWorldViewActive()) {
          particleSys.render();
        }

        // --- End pass 1 (replaces endSwapChainRenderPass) ---
        vkh::renderer::endOneXPass(commandBuffer);

        postProcessingSys.run(vkh::renderer::getCurrentImageIndex());

        // --- HUD pass: draw directly onto the swapchain image, after
        // tonemapping/postprocessing ---
        vkh::renderer::beginHudPass(context, commandBuffer);
        ui.render();
        vkh::renderer::endHudPass(context, commandBuffer);

        vkh::renderer::endFrame(context);
      }
    }
    vkDeviceWaitIdle(context.vulkan.device);
  }
  vkh::renderer::cleanup(context);
  vkh::cleanup(context);
  cleanupWindow(context);
  vkh::audio::cleanup();
}

#if defined(WIN32) && defined(NDEBUG)
#include <windows.h>
int WINAPI WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, PSTR lpCmdLine,
                   int nCmdShow) {
#else
int main() {
#endif
  try {
    run();
  } catch (const std::exception &e) {
    std::println("{}", e.what());
    return EXIT_FAILURE;
  }
  return EXIT_SUCCESS;
}
