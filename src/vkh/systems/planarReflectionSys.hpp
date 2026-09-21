#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../engineContext.hpp"
#include "system.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace vkh {

class EngineContext;
class Image2D;
template <typename T> class Buffer;

class PlanarReflectionSys : public System {
public:
  // `resolution` defaults to a quarter of a common 1080p-ish target
  // (960x540) - planar reflections read believably even fairly low-res
  // since water's own ripple distortion (see water.frag's `distortion`
  // constant) already breaks up sharp edges; halve further if profiling
  // says the extra opaque scene render is too expensive per lake (see the
  // class doc comment's per-lake cost note).
  PlanarReflectionSys(EngineContext &context,
                      glm::uvec2 resolution = {960, 540});
  ~PlanarReflectionSys();

  // Computes the mirrored camera for a water plane at world Y ==
  // `planeY`, reading the REAL camera straight from context.camera
  // (viewMatrix/projectionMatrix - see main.cpp's own GlobalUbo fill,
  // which reads the exact same fields) and context.window
  // (size/aspectRatio) - no parameters needed beyond planeY since
  // everything else this needs is already reachable through `context`
  // (inherited from System). Immediately writes this frame's mirrored
  // GlobalUbo buffer (see getReflectionGlobalSet()). Call once per frame,
  // before recordReflectionPass(), and before
  // vkh::renderer::beginMsaaPass() - Vulkan doesn't allow starting a new
  // rendering scope while already inside another one, and main.cpp's own
  // render loop calls beginMsaaPass immediately followed by
  // skySys.render()/entitySys.render()/lakeWater->render(), so this
  // entire reflection pass has to complete (and land the color target in
  // eShaderReadOnlyOptimal) strictly before that point.
  //
  // Reflection math: mirrors world-space POSITIONS across the plane via
  // p.y -> 2*planeY - p.y, and mirrors world-space DIRECTIONS (which have
  // no position to reflect) by flipping their Y component, d.y -> -d.y.
  // CONFIRMED (not just assumed) against main.cpp's own
  // `glm::rotation(glm::vec3(0.f, 0.f, -1.f), forward)` boid-orientation
  // code: this engine's forward convention is -Z, matching
  // computeMirroredView()'s `-invView[2]` extraction below.
  void updateReflectionCamera(float planeY);

  // Binds this frame's mirrored GlobalUbo (see updateReflectionCamera())
  // at set 0 and renders the opaque scene into this system's offscreen
  // color+depth target. `drawOpaqueScene` should call, at minimum,
  // skySys.render(getReflectionGlobalSet()) and
  // entitySys.render(getReflectionGlobalSet()) - NOT
  // lakeWater->render() (no reflection-of-a-reflection), and not
  // anything else that itself samples SSR/planar reflections. Ends by
  // transitioning the color target to eShaderReadOnlyOptimal so WaterSys
  // can sample it later this same frame.
  //
  // See class doc comment limitation #3 re: dynamic rendering vs your
  // engine's actual render-pass model - main.cpp's own comment ("MSAA
  // render... replaces beginSwapChainRenderPass + subpass 0") suggests
  // this engine has already moved off explicit VkRenderPass/subpasses
  // for its main pass too, which makes dynamic rendering a reasonable
  // bet here, but I still don't have renderer.cpp to confirm outright.
  void recordReflectionPass(
      vk::CommandBuffer cmd,
      const std::function<void(vk::CommandBuffer)> &drawOpaqueScene);

  // For water.cpp's WaterSys::getPlanarReflectionImageInfo() forwarder.
  // Returns THIS frame's slot (see colorTargets' doc comment on why
  // there's one set of targets per frame-in-flight).
  VkDescriptorImageInfo getReflectionImageInfo() const;

  // True once recordReflectionPass() has run at least once for the
  // CURRENT frame-in-flight slot (context.frameInfo.frameIndex) - a
  // caller deciding whether to skip this frame's reflection pass for a
  // culled/off-screen lake (see main.cpp's reflection loop) MUST still
  // force it through on any slot where this is false, or
  // getReflectionImageInfo() will report eShaderReadOnlyOptimal for an
  // image that's actually still sitting in its construction-time
  // eUndefined layout - a lie about the image's real state that only
  // shows up on whichever slot happened to never render (e.g. a lake
  // that starts off-camera might render frame-in-flight slot 0's
  // reflection but never slot 1's, if the camera doesn't happen to swing
  // back to it on an odd frame).
  bool hasEverRenderedCurrentSlot() const {
    return everRendered[context.frameInfo.frameIndex];
  }

  // For water.cpp's WaterSys::getPlanarReflectionViewProj() forwarder -
  // the mirrored proj*view updateReflectionCamera() last computed,
  // consumed by water.tese to place each vertex within this target.
  glm::mat4 getReflViewProj() const {
    return reflProjViews[context.frameInfo.frameIndex];
  }

  // The plane most recently passed to updateReflectionCamera() (world Y
  // ==, converted to plane-equation form) - pass this straight through to
  // EntitySys::render()'s clipPlane parameter for whichever draw goes
  // into THIS lake's reflection target, so its opaque scene render clips
  // out anything below the water surface (the lake bed/underlying
  // terrain) instead of the mirrored camera rendering straight through
  // it once it pitches enough to look back up at the "floor". Like
  // getReflViewProj(), this is a single field rather than one per frame-
  // in-flight slot - unlike that field, that's fine here: the plane only
  // depends on THIS lake's fixed world Y (set once at construction, never
  // per-frame-varying), not on any per-frame render state, so there's
  // nothing for separate slots to ever disagree about.
  glm::vec4 getClipPlane() const { return clipPlane; }

  // The mirrored-camera global descriptor set for the CURRENT frame -
  // bind this at set 0 (in place of
  // context.vulkan.globalDescriptorSets[frameIndex]) for any draw that
  // should render into the reflection target. Binary-compatible with
  // every existing pipeline's set-0 layout since it's allocated from
  // the SAME context.vulkan.globalDescriptorSetLayout and holds a real
  // vkh::GlobalUbo - no shader changes needed anywhere else.
  vk::DescriptorSet getReflectionGlobalSet() const {
    return reflectionGlobalSets[context.frameInfo.frameIndex];
  }

private:
  glm::mat4 computeMirroredView(const glm::mat4 &realView, float planeY) const;

  glm::uvec2 resolution;
  // One set of MSAA/resolved/depth targets PER FRAME-IN-FLIGHT - NOT a
  // single shared set. This mirrors reflectionGlobalUBOs below (see its
  // own comment: "a frame still in flight may be reading last frame's
  // copy while the CPU writes this frame's") - that reasoning applies
  // just as much to these image resources as it does to the UBOs, and
  // missing it here originally meant frame N's reflection pass could be
  // writing into the SAME image frame N-1's water draw was still
  // sampling on the GPU, a genuine data race (manifests as flickering/
  // interference-pattern corruption, not a clean "no reflection" - the
  // image is actively being torn between two frames' content, not
  // simply empty).
  //
  // colorTarget[i]/depthTarget[i] are the MSAA attachments actually
  // drawn into by drawOpaqueScene (see recordReflectionPass()) -
  // SkySys's and EntitySys's pipelines were built against the main
  // scene's own MSAA color pass (format eR8G8B8A8Unorm,
  // context.vulkan.msaaSamples samples - confirmed via validation
  // output, NOT the R16G16B16A16Sfloat single-sample guess this used to
  // be built with), and dynamic rendering requires the bound pipeline's
  // declared attachment format/sample count to match VkRenderingInfo's
  // exactly (VUID-vkCmdDraw-dynamicRenderingUnusedAttachments-08910 /
  // -multisampledRenderToSingleSampled-07285) - there's no format/sample
  // conversion happening implicitly. resolvedColorTarget[i] is the
  // single-sample, sampled resolve target water.frag actually reads
  // (see getReflectionImageInfo()) - colorTarget[i] itself is MSAA and
  // can't be sampled directly as a plain sampler2D.
  std::vector<Image2D> colorTargets;
  std::vector<Image2D> resolvedColorTargets;
  std::vector<Image2D> depthTargets;
  // Per-slot equivalent of main.cpp's old per-lake "ever rendered" flag -
  // now needs to be per FRAME-IN-FLIGHT SLOT, not just per lake, since
  // each slot has its own physical image that independently starts in
  // eUndefined until ITS first recordReflectionPass() call. See
  // hasEverRenderedCurrentSlot()'s doc comment.
  std::vector<bool> everRendered;
  vk::Sampler sampler; // clamp-to-edge - see constructor
  glm::vec4 clipPlane{0.f, 1.f, 0.f, 1e9f}; // see getClipPlane()
  // One per frame-in-flight slot, matching colorTargets/resolvedColorTargets/
  // depthTargets above - NOT a single shared glm::mat4 (what this used to
  // be). getReflectionImageInfo() always returns the image for the CURRENT
  // slot; if reflProjView were a single field, a frame where this lake is
  // skipped (invisible, see main.cpp's reflection loop) would leave it
  // holding whatever the LAST actual render computed - which may have been
  // for a DIFFERENT slot than the one getReflectionImageInfo() returns this
  // frame. That's not just staleness, it's an outright mismatch between the
  // viewProj water.tese uses and the texture water.frag samples, and reads
  // exactly like "reflection is suddenly wrong" rather than "reflection is
  // a few frames behind."
  std::vector<glm::mat4> reflProjViews;

  // One per swapchain image, same reasoning as
  // context.vulkan.globalUBOs itself (see main.cpp) - a frame still in
  // flight may be reading last frame's copy while the CPU writes this
  // frame's. Uses the real vkh::GlobalUbo type directly (not a
  // hand-duplicated struct) so there's no risk of drifting out of sync
  // with engineContext.hpp, and so .write(&ubo) can rely on the same
  // typed-buffer convenience overload main.cpp itself uses for
  // context.vulkan.globalUBOs.
  std::vector<Buffer<GlobalUbo>> reflectionGlobalUBOs;
  std::vector<vk::DescriptorSet> reflectionGlobalSets;
};

} // namespace vkh
