#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include <memory>
#include <vector>

#include "../computePass.hpp"

namespace vkh {

class EngineContext;
class Image2D;

// Matches BlackHoleGodRaysPush in godrays.comp.
struct BlackHoleGodRaysPush {
  glm::vec4 tintAndIntensity; // rgb = ray tint, a = intensity
  glm::vec4 rayParams;        // x=density, y=decay, z=weight, w=exposure - see
                              // GodRaysEffect's member comments below
  glm::vec4 maskParams; // x=diskLuminanceThreshold, y=starLuminanceThreshold,
                        // z=starWeight, w unused
};

// Screen-space volumetric light-shaft ("god ray") effect for a black
// hole's accretion disk. This is the classic Kenny Mitchell / GPU Gems
// "volumetric light scattering" radial-blur technique - each pixel
// samples toward the light's screen position, accumulating an
// exponentially-decaying, luminance-masked contribution - adapted for a
// genuine world-space point light instead of an infinitely-distant
// directional sun.
//
// Where the black hole IS, and where that projects to on screen, are
// both computed entirely inside godrays.comp itself (see
// lightScreenPosAndVisibility() there), from bhWorldPos() in the shared
// blackHoleOrbit.glsl - the same function sky.frag uses to place the
// black hole's own rendered diorama. This class deliberately holds no
// position of its own and does no projection math: there is exactly one
// place in the engine that decides where the black hole is, so the rays
// can't end up pointed somewhere the black hole visually isn't.
//
// Self-contained like SmokeCompositeEffect otherwise: the fixed 0-2
// bindings ComputePass wires for every effect (output image, scene color,
// depth - depth is unused in THIS shader, godrays.comp; the heavy work
// that does use it now lives in godraysGenerate.comp - see below) plus
// one additional binding (3) for its own half-res ray image, wired
// through additionalBindings()/writeAdditionalDescriptors() same as any
// other effect would add a binding.
//
// SPLIT INTO TWO SHADERS as of the RenderDoc-measured ~40ms/frame full-
// native-resolution cost (see chat): godraysGenerate.comp does the actual
// expensive 48-sample-per-pixel radial march, at HALF resolution, into an
// image this class owns and manages entirely itself (raysImages,
// generatePipeline, generateSetLayout, generateDescriptorSets below) -
// dispatched from beforeDispatch(), a hook ComputeEffect already provides
// for exactly this ("effects that need to write into their own auxiliary
// resources before the main shader reads them" - see computePass.hpp).
// godrays.comp (the shader ComputePass itself drives via the normal fixed
// dispatch) is now just a cheap bilinear upsample + additive blend at
// full resolution. See beforeDispatch()'s own doc comment for the full
// mechanics and the one behavioral nuance this split introduces (the
// generate pass always samples the TRUE resolved scene, not a
// chained-in previous stage's output).
//
// NOTE on chaining: unchanged from before the split - like every
// ComputeEffect, godrays.comp's binding-1 sceneColor still reads
// whatever ComputePass's chaining machinery gives it (the real resolved
// scene color, or a previous stage's hand-off image - see
// InputTargetProvider in computePass.hpp), so this effect still
// composites correctly when chained after e.g. SmokeCompositeEffect.
class GodRaysEffect : public ComputeEffect<BlackHoleGodRaysPush> {
public:
  using Push = BlackHoleGodRaysPush;

  explicit GodRaysEffect(EngineContext &context);
  ~GodRaysEffect();

  // Ray tint (default: a slight blue-shift, since "light disappearing
  // into/bending around a black hole" reads better cool than the
  // classic warm sun-shaft yellow - see chat). Intensity is a plain
  // multiplier on the final accumulated ray color.
  void setTint(const glm::vec3 &color) { tint = color; }
  void setIntensity(float value) { intensity = value; }

  std::vector<vk::DescriptorSetLayoutBinding>
  additionalBindings() const override;
  void writeAdditionalDescriptors(DescriptorWriter &writer,
                                  uint32_t imageIndex) override;
  const char *shaderPath() const override {
    return "shaders/postprocessing/godrays.comp.spv";
  }
  const char *debugName() const override { return "black hole god rays"; }
  Push makePushConstants() const override;

  // Dispatches godraysGenerate.comp (the expensive half-res march) into
  // raysImages[imageIndex] - see this class's doc comment above and this
  // method's definition in the .cpp for the full mechanics.
  void beforeDispatch(vk::CommandBuffer cmd, uint32_t imageIndex) override;
  // Resizes/recreates raysImages and generateDescriptorSets for the new
  // image count AND the new context.window.size - see
  // recreateRaysImages()'s doc comment for why this has to happen on
  // every call, not just when imageCount itself actually changes.
  void onImageCountChanged(uint32_t imageCount) override;

private:
  EngineContext &context;

  glm::vec3 tint{0.75f, 0.85f, 1.f};
  float intensity{1.f};

  // Radial-sample tuning (see godraysGenerate.comp's NUM_SAMPLES loop).
  // These are visual-tuning constants rather than per-frame gameplay
  // state - add setters for these individually if you end up wanting to
  // expose them (e.g. to a settings/UI panel), same as SkySys's
  // bake-resolution constructor params became live settings later.
  //   density  - how far each sample step reaches toward the light,
  //              relative to the full pixel-to-light distance.
  //   decay    - per-sample exponential falloff (closer to 1 = longer
  //              rays).
  //   weight   - per-sample contribution multiplier before decay.
  //   exposure - final multiplier on the accumulated ray color.
  float density{0.85f};
  float decay{0.97f};
  float weight{0.55f};
  float exposure{0.35f};

  // Two separate luminance bands, not one threshold - see
  // godraysGenerate.comp's diskMask/starMask. diskLuminanceThreshold
  // isolates just the black hole's own genuinely-bright light, at full
  // strength; raised from this effect's first version (was 0.6) because
  // the milky way's own brighter nebula clouds were clearing that value
  // too, contributing at FULL ray strength and blowing out the whole sky
  // - see chat. starLuminanceThreshold/starWeight add back a second, much
  // dimmer band so ordinary starfield/nebula brightness still contributes
  // its own faint rays (as requested) without repeating that blowout:
  // starWeight is a hard multiplier on top of the star band, so even a
  // fully-bright star pixel contributes only starWeight's fraction of
  // what an equally-bright disc pixel would.
  float diskLuminanceThreshold{0.85f};
  float starLuminanceThreshold{0.2f};
  float starWeight{0.05f};

  // --- Half-resolution generate pass - see class doc comment above ---

  // Each axis divided by this before creating raysImages/dispatching
  // godraysGenerate.comp. 2 (quarter the pixels) is a conservative first
  // cut; raise to 4 for a bigger win if half-res rays still look sharper
  // than they need to.
  static constexpr uint32_t kResolutionDivisor = 2;

  void createGeneratePipeline();
  // (Re)creates raysImages/generateDescriptorSets sized from the CURRENT
  // context.window.size and the given swapchain image count - called
  // from the constructor and from onImageCountChanged(). Must be re-run
  // on every onImageCountChanged() call, not just when imageCount itself
  // differs from before: that hook fires on every swapchain
  // recreation event including a plain window resize with the same
  // image count (see ComputePass::recreateDescriptors()), and a resize
  // is exactly when raysWidth_/raysHeight_ need to change too.
  void recreateRaysImages(uint32_t imageCount);

  bool useSkyMask{true};
  vk::Sampler raysSampler{}; // linear + clamp - upsampling a lower-res
                             // image REQUIRES linear filtering, unlike
                             // PostProcessingSys's same-resolution
                             // nearest-filtered chain hand-off sampler.
  vk::DescriptorSetLayout generateSetLayout{};
  std::unique_ptr<ComputePipeline> generatePipeline;
  std::unique_ptr<ComputePipeline> maskPipeline;
  std::vector<vk::DescriptorSet> generateDescriptorSets; // one per
                                                         // swapchain image
  // One per swapchain image, matching generateDescriptorSets/imageIndex.
  // shared_ptr (not a value vector) so this header doesn't need Image2D's
  // full definition - same pattern ProceduralGrassSys::HeightmapBinding
  // uses for the same reason.
  std::vector<std::shared_ptr<Image2D>> raysImages;
  std::vector<std::shared_ptr<Image2D>> skyMasks;
  uint32_t raysWidth_{0};
  uint32_t raysHeight_{0};
};

} // namespace vkh
