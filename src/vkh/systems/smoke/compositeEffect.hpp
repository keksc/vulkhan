#pragma once

#include <vulkan/vulkan.hpp>

#include "../../buffer.hpp"
#include "../../image.hpp"
#include "../../pipeline.hpp"
#include "../computePass.hpp"
#include "lights.hpp"
#include "smoke.hpp"

#include <memory>
#include <vector>

namespace vkh {

// Matches SmokePush in postProcessing.comp.
struct SmokeCompositePush {
  glm::vec4 boxMin;     // xyz = gridOrigin * cellSize, w = cellSize
  glm::ivec4 cellCount; // xyz used
  // Mirrors TransmittancePush::maxLightDistance below - lights farther
  // than this from the grid's AABB are skipped entirely in
  // postProcessing.comp's per-step light loop, fading out smoothly over
  // the last quarter of the range like the transmittance precompute does.
  float maxLightDistance;
  // Total active lights - drives the direct-lighting loop in
  // postProcessing.comp. May exceed shadowedLightCount.
  int lightCount;
  // min(lightCount, TRANSMITTANCE_CAPACITY) - how many of the lights
  // above have a written, valid transmittance descriptor to sample.
  // Lights at index >= shadowedLightCount are still lit, just unshadowed
  // (treated as trans = 1.0) - see postProcessing.comp's main loop.
  int shadowedLightCount;
};

// Matches TransmittancePush in transmittance.comp.
struct TransmittancePush {
  glm::ivec4 cellCount;  // xyz used
  glm::ivec4 gridOrigin; // xyz used
  float cellSize;
  // Lights farther than this from the grid's world-space AABB are skipped
  // by the shader (treated as fully transmissive, no march).
  float maxLightDistance;
  // How many transmittance volume slots to fill this dispatch - see
  // SmokeCompositePush::shadowedLightCount above, same value.
  int shadowedLightCount;
};

// The ComputeEffect for postProcessing.comp: composites the resolved scene
// color, depth, smoke dye volume, and an arbitrary set of point lights
// (see lights.hpp's MAX_LIGHT_CAPACITY/TRANSMITTANCE_CAPACITY) into the
// swapchain image.
class SmokeCompositeEffect : public ComputeEffect<SmokeCompositePush> {
public:
  using Push = SmokeCompositePush;

  SmokeCompositeEffect(EngineContext &context, SmokeSys &smokeSys);
  ~SmokeCompositeEffect() override;

  // Fixed-capacity API: count is clamped to MAX_LIGHT_CAPACITY. Writes
  // straight into the mapped, host-coherent lightsBuffer - safe to call
  // from anywhere outside of ComputePass::run(), since the compute pass
  // only ever reads this buffer, never writes it.
  void setPointLights(const PointLight *lights, int count);

  // Precomputes per-cell transmittance toward every shadowed light (see
  // shadowedLightCount) into transmittanceVolumes (see transmittance.comp),
  // replacing what used to be a per-pixel-per-raymarch-step secondary
  // shadow march inside postProcessing.comp. Overrides
  // ComputeEffect::beforeDispatch, so ComputePass calls this automatically
  // once per frame, right before the main composite dispatch.
  //
  // Issues the barrier the base class docs require: the written slots of
  // transmittanceVolumes[imageIndex] go from storage-write here to
  // sampled-read in the main dispatch that follows in the same command
  // buffer (both images stay eGeneral, so a plain execution+memory
  // barrier, no layout transition, is enough).
  //
  // If every active light is farther than maxLightDistance() from the
  // grid, the dispatch (and its barrier) is skipped entirely for this
  // frame - see the .cpp for why that's safe.
  void beforeDispatch(vk::CommandBuffer cmd, uint32_t imageIndex) override;

  // Resizes transmittanceVolumes (and their descriptor sets) to match a
  // new swapchain image count. See ComputeEffect::onImageCountChanged.
  void onImageCountChanged(uint32_t imageCount) override;

  // Adds binding 3 (smoke dye volume), binding 4 (point lights SSBO), and
  // binding 5 (descriptor-indexed transmittance volume array) on top of
  // the 0-2 that ComputePass wires for every effect.
  std::vector<vk::DescriptorSetLayoutBinding>
  additionalBindings() const override;
  std::vector<vk::DescriptorBindingFlags>
  additionalBindingFlags() const override;
  const char *shaderPath() const override {
    return "shaders/smokeCompositeEffect.comp.spv";
  }
  const char *debugName() const override { return "post processing"; }
  void writeAdditionalDescriptors(DescriptorWriter &writer,
                                  uint32_t imageIndex) override;
  Push makePushConstants() const override;

private:
  void createLightsBuffer();
  void createTransmittancePass();
  void createTransmittanceImages(uint32_t imageCount);
  void writeTransmittanceDescriptorSet(uint32_t imageIndex);

  // Grid diagonal * kMaxLightDistanceFactor: lights farther than this from
  // the grid's AABB contribute negligible attenuation to visible smoke, so
  // transmittance.comp skips marching them entirely, and if EVERY active
  // light is beyond this, beforeDispatch() skips the whole pass for the
  // frame.
  float maxLightDistance() const;

  EngineContext &context;
  SmokeSys &smokeSys;

  // Dedicated sampler for the dye volume: it's periodic, so it needs
  // REPEAT addressing on all three axes.
  vk::Sampler smokeVolumeSampler;

  // Storage buffer (SSBO), not a uniform buffer: a UBO can't hold a
  // runtime-sized array as cleanly as an SSBO, and SSBOs are the
  // conventional choice for "array of structs, possibly large". Allocated
  // once at MAX_LIGHT_CAPACITY - see lights.hpp - and never resized.
  // Light count itself now travels via push constant (SmokeCompositePush::
  // lightCount), not a header field in the buffer, since the buffer is a
  // bare array with no room for one.
  std::unique_ptr<Buffer<PointLight>> lightsBuffer;
  std::vector<PointLight> lightsCpuCopy;
  int activeLightCount = 0;

  // --- Transmittance precompute pass (see beforeDispatch()) ---

  // Single-channel (r32f) per light, one slot per active shadowed light,
  // rather than the old 2-volume/4-lights-per-channel rgba32f packing.
  // Same total bytes for a given light count (4 floats either way), but a
  // dedicated descriptor per light lets it be indexed directly - no
  // volume/channel math - via descriptor indexing
  // (VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT) rather than needing a
  // fixed compile-time packing factor. Costs one full trilinear fetch per
  // shadowed light instead of one fetch covering 4 - see the perf note in
  // chat; unlikely to matter until light counts get large.
  //
  // Sized TRANSMITTANCE_CAPACITY slots per swapchain image, allocated
  // once at that capacity - only the first shadowedLightCount slots are
  // written by beforeDispatch()/transmittance.comp each frame, and only
  // those are given valid descriptors (PARTIALLY_BOUND_BIT means the rest
  // can stay unwritten - the shaders never read past shadowedLightCount).
  //
  // One set of slots PER swapchain image, not shared: written every frame
  // in beforeDispatch() and read every frame by the main composite
  // dispatch that immediately follows, all within ComputePass's shared
  // per-frame command buffer with no CPU-side wait in between. A single
  // shared set would let frame N+1's write race frame N's still-in-flight
  // read with multiple frames in flight. Indexing by imageIndex closes
  // that hole for free - see the original member comment history for the
  // full reasoning (unchanged from the rgba32f version).
  //
  // Memory note: TRANSMITTANCE_CAPACITY * imageCount Image3D volumes at
  // grid resolution - e.g. TRANSMITTANCE_CAPACITY=64, a 64^3 grid, 3
  // swapchain images = 64 * 3 * 64*64*64*4 bytes = ~201 MB. This is the
  // real cost of "raise the light cap" - not compute time, VRAM. Keep
  // TRANSMITTANCE_CAPACITY only as high as you actually need shadowed.
  std::vector<std::vector<Image3D>> transmittanceVolumes;

  // Own tiny descriptor set (one per swapchain image), deliberately
  // separate from SmokeSys's private 7-binding compute set: this pass
  // only touches the dye volume (read), its own transmittance volumes
  // (write), and the lights SSBO. Binding 1 (transmittance volumes) uses
  // a descriptor-indexed array sized TRANSMITTANCE_CAPACITY with
  // VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT set at layout-creation time
  // - see createTransmittancePass() in the .cpp for the exact flags, and
  // note this requires VkPhysicalDeviceDescriptorIndexingFeatures::
  // descriptorBindingPartiallyBound enabled on the device (and
  // descriptorBindingStorageImageUpdateAfterBind /
  // descriptorBindingSampledImageUpdateAfterBind if you also want
  // update-after-bind semantics - not required for the partially-bound-
  // only approach used here).
  vk::DescriptorSetLayout transmittanceSetLayout;
  std::vector<vk::DescriptorSet> transmittanceSet;
  vk::PipelineLayout transmittancePipelineLayout;
  std::unique_ptr<ComputePipeline> transmittancePipeline;
};

} // namespace vkh
