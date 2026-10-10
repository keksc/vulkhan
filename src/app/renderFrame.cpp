#include "renderFrame.hpp"
#include "../UI.hpp"
#include "../settings.hpp"
#include "../vkh/cpuProfiler.hpp"
#include "../vkh/gpuProfiler.hpp"
#include "../vkh/renderer.hpp"
#include "../vkh/systems/entity/entities.hpp"
#include "../vkh/systems/grass/proceduralGrass.hpp"
#include "../vkh/systems/particles.hpp"
#include "../vkh/systems/planarReflectionSys.hpp"
#include "../vkh/systems/postProcessing.hpp"
#include "../vkh/systems/sky.hpp"
#include "../vkh/systems/terrain/terrain.hpp"
#include "../vkh/systems/tree/treeSys.hpp"
#include "../vkh/systems/water/water.hpp"
#include "../vkh/systems/wolf/wolfSys.hpp"
#include "lakes.hpp"
#include "math.hpp"
namespace app {
void renderFrame(vkh::EngineContext &context, RenderSystems systems,
                 LakeStreaming &lakes) {
  auto &[ui, terrainSys, entitySys, wolfSys, treeSys, grassSys, skySys,
         particleSys, postProcessingSys, gpuProfiler] = systems;
  auto commandBuffer = context.frameInfo.cmd;
  auto &activeLakes = lakes.active();
  auto &reflectionGroups = lakes.groups();
  gpuProfiler.mark(commandBuffer, 1);

  // Planar reflections: one pass per lake, into that lake's own
  // offscreen target, strictly before beginMsaaPass() below - see
  // PlanarReflectionSys::updateReflectionCamera()'s doc comment on
  // why (Vulkan doesn't allow starting a new rendering scope while
  // already inside another one). Only the opaque scene (sky +
  // entities, NOT water itself - no reflection-of-a-reflection)
  // goes into each reflection target.
  //
  // A lake only gets a fresh reflection this frame if it's inside
  // the REAL camera's frustum - each one is a full extra opaque-
  // scene render (mirrored re-cull + sky/entities/trees), so this
  // does cost proportionally to how many lakes are visible at once.
  // An earlier version of this also capped the count to the
  // closest few visible lakes per frame, reusing a stale texture
  // for the rest - reverted (see chat): a visible lake showing a
  // frozen reflection while the real scene keeps moving broke the
  // illusion far more than the frame-time cost of rendering it was
  // worth. Off-screen lakes still reuse their last-rendered
  // texture via PlanarReflectionSys::hasEverRenderedCurrentSlot(),
  // same mechanism that already covers a frame-in-flight slot that
  // has never rendered (each slot's image starts in eUndefined and
  // only WaterSys's own descriptor claims eShaderReadOnlyOptimal
  // regardless of whether that's actually true yet) - forced
  // through in that case regardless of visibility. Staleness there
  // is genuinely invisible: nothing on screen samples an off-screen
  // lake's reflection texture.
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

    lakes.updateVisibility(camFrustum, context.camera.position);

    // Which reflection groups get a fresh pass this frame. Groups with
    // a visible lake within kReflectionAlwaysDist of the camera, and
    // any frame-in-flight slot that never rendered (its image is still
    // eUndefined), ALWAYS refresh - a nearby lake with a lagging
    // reflection is exactly what broke the illusion before. Farther
    // visible groups share a small per-frame budget, stalest (and
    // nearest) first, so with many distinct water levels on screen the
    // far ones refresh every few frames instead of every frame. A
    // skipped group keeps its own slot's matching texture + matrix
    // (see PlanarReflectionSys::reflProjViews), so it stays internally
    // consistent, just a few frames old.
    constexpr float kReflectionAlwaysDist = 600.f;
    constexpr size_t kFarReflectionBudget = 2;
    const glm::vec2 camXZ(context.camera.position.x, context.camera.position.z);
    std::vector<ReflectionGroup *> refresh;
    std::vector<std::pair<float, ReflectionGroup *>> deferrable;
    for (auto &groupPtr : reflectionGroups) {
      auto &group = *groupPtr;
      if (group.lakes.empty())
        continue;
      bool visible = false;
      float nearest = std::numeric_limits<float>::max();
      for (const ActiveLake *al : group.lakes)
        if (al->visible) {
          visible = true;
          nearest = std::min(nearest, lakes.distance(camXZ, al->lake));
        }
      const bool slotNeverRendered = !group.refl->hasEverRenderedCurrentSlot();
      if (!visible) {
        if (slotNeverRendered)
          refresh.push_back(&group);
      } else if (slotNeverRendered || group.lastRefreshFrame == 0 ||
                 nearest < kReflectionAlwaysDist) {
        refresh.push_back(&group);
      } else {
        const float staleness =
            static_cast<float>(lakes.frame() - group.lastRefreshFrame);
        deferrable.push_back({staleness / (1.f + nearest / 500.f), &group});
      }
    }
    std::sort(deferrable.begin(), deferrable.end(),
              [](const auto &x, const auto &y) { return x.first > y.first; });
    for (size_t i = 0; i < deferrable.size() && i < kFarReflectionBudget; ++i)
      refresh.push_back(deferrable[i].second);

    for (ReflectionGroup *groupPtr : refresh) {
      auto &group = *groupPtr;
      auto &planarRefl = *group.refl;
      group.lastRefreshFrame = lakes.frame();

      planarRefl.updateReflectionCamera(group.planeY);
      entitySys.cullForViewProj(commandBuffer, planarRefl.getReflViewProj());
      // Same TreeSys as the main view: this pass re-culls its instance
      // set against the mirrored camera (the main cull below runs
      // after every reflection pass, and the first cull of the frame
      // also uploads a newly built instance set to VRAM).
      treeSys.cullForViewProj(commandBuffer, planarRefl.getReflViewProj());
      planarRefl.recordReflectionPass(
          commandBuffer, [&](vk::CommandBuffer reflCmd) {
            vk::DescriptorSet reflSet = planarRefl.getReflectionGlobalSet();
            // Cull against the REFLECTION camera's frustum, not the
            // main camera's, or chunks pop in/out of the reflection.
            const glm::mat4 reflVP = planarRefl.getReflViewProj();
            // 640x360 target + rippled water: every 4th vertex (16x
            // fewer triangles) and nothing past 2.5 km is plenty.
            terrainSys.render(reflSet, planarRefl.getClipPlane(), &reflVP,
                              {.indexLod = 2, .maxDistance = 2500.f});
            entitySys.render(reflSet, planarRefl.getClipPlane());
            wolfSys.render(reflSet, planarRefl.getClipPlane(), &reflVP);
            // Only the reflection-visible tiers: low-poly mid-range
            // trees plus low-poly stand-ins for the near ones.
            treeSys.render(reflSet, planarRefl.getClipPlane(),
                           vkh::TreeSys::ePassReflection);
            skySys.render(reflSet);
          });
    }

    // Restore the real camera's culled set - see the comment above
    // on why this must run after every reflection pass this frame,
    // not before. Cheap even when no lake was rendered above (the
    // cull done during the reflection loop above replaces what
    // this same call would otherwise have produced earlier).
    entitySys.cull(commandBuffer);
    treeSys.cull(commandBuffer);

    // each lake's update() reads getPlanarReflectionViewProj()
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
    // For a lake this frame's loop SKIPPED (off-screen - see
    // hasEverRenderedCurrentSlot()), getPlanarReflectionViewProj()
    // correctly returns THAT lake's last-actually-rendered matrix,
    // matching its last-actually-rendered (now stale, but still
    // internally consistent) texture - so an off-screen lake's
    // reflection may lag the camera by several frames, but won't be
    // internally distorted/mismatched the way the swimming case
    // was, and it's not visible anyway so the staleness itself
    // never shows.
    //
    // The first lake drives waveSim's simulation step this frame;
    // every other lake just re-samples the resulting texture, so
    // pass driveSimulation = false for them. This is a compute
    // dispatch (modelTess.recordComputeWaves()) - safe to record
    // here since nothing between here and beginMsaaPass below has
    // an active vkCmdBeginRendering scope open (each lake's
    // recordReflectionPass() call above already closed its own).
    bool firstLake = true;
    for (auto &al : activeLakes) {
      if (!al.visible)
        continue; // hidden lakes need no wave update or reflection
      al.water->update(/*driveSimulation=*/firstLake);
      firstLake = false;
    }
  }

  // --- Pass 0: MSAA render, resolving into scene color + resolved
  // depth (replaces beginSwapChainRenderPass + subpass 0) ---
  gpuProfiler.mark(commandBuffer, 2);
  vkh::renderer::beginMsaaPass(context, commandBuffer);

  if (ui.isWorldViewActive()) {
    terrainSys.render();
    entitySys.render();
    wolfSys.render();
    treeSys.render();
    if (vkh::settings::current().drawGrass)
      grassSys.render();
    skySys.render();
    for (auto &al : activeLakes)
      if (al.visible)
        al.water->render();
  }

  // --- Transition to pass 1: 1x direct render onto resolved buffers
  // (replaces vkCmdNextSubpass) ---
  vkh::renderer::endMsaaPass(commandBuffer);
  gpuProfiler.mark(commandBuffer, 3);
  if (ui.isWorldViewActive())
    wolfSys.rasterizeFur();
  gpuProfiler.mark(commandBuffer, 4);
  vkh::renderer::beginOneXPass(context, commandBuffer);

  if (ui.isWorldViewActive()) {
    wolfSys.renderFur();
    particleSys.render();
  }

  // --- End pass 1 (replaces endSwapChainRenderPass) ---
  vkh::renderer::endOneXPass(commandBuffer);

  gpuProfiler.mark(commandBuffer, 5);
  postProcessingSys.run(vkh::renderer::getCurrentImageIndex());
  gpuProfiler.mark(commandBuffer, 6);

  // --- HUD pass: draw directly onto the swapchain image, after
  // tonemapping/postprocessing ---
  vkh::renderer::beginHudPass(context, commandBuffer);
  ui.render();
  vkh::renderer::endHudPass(context, commandBuffer);

  gpuProfiler.mark(commandBuffer, 7);
  vkh::profileCpu("frame submit/present",
                  [&] { vkh::renderer::endFrame(context); });
}
} // namespace app
