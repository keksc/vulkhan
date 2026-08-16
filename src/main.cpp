#include <GLFW/glfw3.h>
#include <glm/gtx/string_cast.hpp>

#include "UI.hpp"
#include "boids.hpp"
#include "modding.hpp"
#include "vkh/audio.hpp"
#include "vkh/camera.hpp"
#include "vkh/cleanup.hpp"
#include "vkh/engineContext.hpp"
#include "vkh/init.hpp"
#include "vkh/input.hpp"
#include "vkh/paths.hpp"
#include "vkh/renderer.hpp"
#include "settings.hpp"
#include "vkh/sceneBuilder.hpp"
#include "vkh/swapChain.hpp"
#include "vkh/systems/entity/entities.hpp"
#include "mountain.hpp"
#include "vkh/systems/particles.hpp"
#include "vkh/systems/postProcessing.hpp"
#include "vkh/systems/sky.hpp"
#include "vkh/systems/smoke/smoke.hpp"
#include "vkh/systems/water/water.hpp"
#include "vkh/window.hpp"

#include "dungeonGenerator.hpp"
#include "network/networkSession.hpp"

#include <algorithm>
#include <chrono>
#include <print>
#include <random>
#include <string>
#include <unordered_map>

std::mt19937 rng{std::random_device{}()};

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

  vkh::EngineContext context{};
  vkh::initWindow(context);
  vkh::init(context);
  vkh::audio::init();
  vkh::input::init(context);

  vkh::renderer::init(context);

  {
    ModManager modMgr;

    // Connect to the vulkhan-server. Throws on failure to connect (see
    // Network's constructor) -- for now let that propagate up to run()'s
    // caller; you'll likely want a retry/offline-mode path instead.
    NetworkSession netSession("127.0.0.1");

    std::chrono::time_point<std::chrono::high_resolution_clock> audioFadeBegin;
    const float audioFadeSpeed = 2.5f;

    // vkh::audio::Sound boringSpeech("sounds/Rhorhorho.opus");
    // boringSpeech.play();
    // vkh::audio::Sound bgm("sounds/Enter Remollon.opus");
    // bgm.play();

    // SkySys doesn't know about Settings (it's graphics-layer code) --
    // main.cpp reads the toggle and passes it in as a plain bool instead.
    vkh::SkySys skySys(context, vkh::settings::current().useCachedSkyBake);
    vkh::EntitySys entitySys(context);

    vkh::SmokeSys smokeSys(context);
    // vkh::WaterSys waterSys(context, skySys);
    vkh::ParticleSys particleSys(context);
    vkh::PostProcessingSys postProcessingSys(context);

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

    // Procedural mountain landscape, using an erosion-noise heightmap
    // (see vkh/systems/mountain/mountain.hpp for details).
    vkh::mountain::MountainCreateInfo mountainInfo{};
    mountainInfo.resolution = {256u, 256u};
    mountainInfo.worldSize = {200.f, 200.f};
    mountainInfo.baseHeight = 0.f;
    mountainInfo.peakHeight = 60.f;
    mountainInfo.octaves = 5;
    mountainInfo.erosionStrength = 1.f;
    auto mountainScene = vkh::mountain::generate(
        context, entitySys.texturesSetLayout, mountainInfo);
    for (size_t i = 0; i < mountainScene->meshes.size(); i++)
      entities.emplace_back(
          vkh::EntitySys::Transform{.position{10.f, 10.f, 10.f}},
          vkh::EntitySys::RigidBody{}, mountainScene, i);

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
      {
        auto pointed = entitySys.getPointingAt(1.0f);
        if (pointed != lastPicked) {
          if (lastPicked) {
            lastPicked->color = glm::vec4(1.0f); // Reset
          }

          lastPicked = pointed;

          if (lastPicked) {
            lastPicked->color =
                glm::vec4(2.0f, 0.5f, 0.5f, 1.0f); // Highlight Red-ish
          }
        }
      }

      // Networking: pull in whatever arrived since last frame, then push
      // our own transform out. Cheap no-op until the Hello handshake has
      // completed (see NetworkSession::sendUpdate).
      netSession.poll();
      netSession.sendUpdate(context.camera.position, glm::quat_cast(context.camera.viewMatrix));

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
        ubo.time = glfwGetTime();
        context.vulkan.globalUBOs[frameIndex].write(&ubo);
        context.vulkan.globalUBOs[frameIndex].flush();

        if (ui.isWorldViewActive()) {
          // waterSys.update();

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
          particleSys.update();
          entitySys.updateJoints();
          entitySys.cull(commandBuffer);
        }
        if (ui.isSmokeViewActive()) {
          smokeSys.update();
        }

        // --- Pass 0: MSAA render, resolving into scene color + resolved
        // depth (replaces beginSwapChainRenderPass + subpass 0) ---
        vkh::renderer::beginMsaaPass(context, commandBuffer);

        if (ui.isWorldViewActive()) {
          skySys.render();
          entitySys.render();
          // waterSys.render();
        }

        // --- Transition to pass 1: 1x direct render onto resolved buffers
        // (replaces vkCmdNextSubpass) ---
        vkh::renderer::transitionToOneXPass(context, commandBuffer);

        if (ui.isWorldViewActive()) {
          particleSys.render();
        }
        if (ui.isSmokeViewActive()) {
          smokeSys.render();
        }

        // --- End pass 1 (replaces endSwapChainRenderPass) ---
        vkh::renderer::endOneXPass(commandBuffer);

        postProcessingSys.run(commandBuffer,
                              vkh::renderer::getCurrentImageIndex());

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
