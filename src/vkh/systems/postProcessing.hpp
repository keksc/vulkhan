#pragma once

#include "computePass.hpp"
#include "system.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace vkh {

class Image2D;
class EngineContext;

// Builds one stage of a PostProcessingSys chain. Given the context plus
// this stage's wiring (where it should write its output, and whether it
// can skip the shared depth/scene-color input barrier - see
// ComputePass's constructor doc comment), constructs and returns the
// fully-built pass behind the type-erased IComputePass interface.
//
// This is the ONLY place a stage's PushT (and its ComputeEffect<PushT>
// subclass) needs to be named - see makeComputePassFactory() below for
// the usual way to build one. PostProcessingSys itself never sees either.
using ComputePassFactory = std::function<std::unique_ptr<IComputePass>(
    EngineContext &, OutputTargetProvider, bool skipInputTransitions)>;

// Convenience helper for building a ComputePassFactory around a single
// ComputeEffect<PushT>. `effectFactory` is called with the EngineContext
// once PostProcessingSys actually constructs this stage, and must return
// a std::unique_ptr<ComputeEffect<PushT>> (or a subclass) - typically a
// small lambda capturing whatever that effect needs (e.g. a SmokeSys&):
//
//   std::vector<vkh::ComputePassFactory> effects;
//   effects.push_back(vkh::makeComputePassFactory<SmokeCompositePush>(
//       [&](EngineContext &ctx) {
//         return std::make_unique<SmokeCompositeEffect>(ctx, smokeSys);
//       }));
//   vkh::PostProcessingSys postProcessingSys(context, std::move(effects));
//
// Any effect-specific configuration (smoke's per-frame point light list,
// etc.) is the caller's responsibility, done directly against whatever
// object `effectFactory` constructs - hang on to it separately (e.g. a
// raw pointer captured alongside) if you need to keep talking to it after
// the chain is built. PostProcessingSys deliberately has no way to get
// back to it.
template <typename PushT, typename EffectFactoryFn>
ComputePassFactory makeComputePassFactory(EffectFactoryFn effectFactory) {
  return [effectFactory](EngineContext &context,
                         OutputTargetProvider outputTarget,
                         bool skipInputTransitions) -> std::unique_ptr<IComputePass> {
    return std::make_unique<ComputePass<PushT>>(
        context, effectFactory(context), std::move(outputTarget),
        skipInputTransitions);
  };
}

// A general, ordered chain of postprocessing compute passes, run back to
// back in the same per-frame command buffer. PostProcessingSys is
// completely effect-agnostic: it knows nothing about what any stage does,
// only that each one satisfies IComputePass (see computePass.hpp) - it
// exposes no effect-specific attribute or method of its own (no smoke
// lighting setter, no SSR toggle, nothing tied to any particular shader).
// Adding, removing, or reordering effects is entirely a matter of what's
// passed into the constructor; this class doesn't change.
//
// Every stage but the last writes into an owned intermediate handoff
// image (see makeIntermediateImages()) instead of the real swapchain
// image, and every stage but the first skips the shared depth/scene-color
// input barrier (the first stage already transitioned them once this
// frame - see ComputePass's skipInputTransitions doc comment). The last
// stage writes the real swapchain image and owns the present-layout
// transition, exactly as a single, unchained ComputePass always has. A
// chain of one stage gets no intermediate image at all - that one stage
// writes the swapchain image directly, same as before any chaining
// existed.
class PostProcessingSys : public System {
public:
  // effectFactories must be non-empty - the last stage owns the real
  // swapchain image and its present-layout transition (see run()), so
  // there's no meaningful zero-stage chain. Order is run order.
  PostProcessingSys(EngineContext &context,
                    std::vector<ComputePassFactory> effectFactories);
  ~PostProcessingSys();

  // Kills the WHOLE chain - run() falls back to a plain copy of the
  // resolved scene color straight into the swapchain image, the same
  // "postprocessing is off" passthrough fallback a single ComputePass has
  // always had. Individual stages can still be toggled on their own via
  // whatever handle the caller kept to them (see makeComputePassFactory's
  // doc comment) - this only gates the chain as a whole.
  void setEnabled(bool value) { enabled = value; }
  bool isEnabled() const { return enabled; }

  void run(uint32_t imageIndex);
  void recreateDescriptors();

private:
  static vk::Sampler makeIntermediateSampler(EngineContext &context);
  static std::vector<Image2D> makeIntermediateImages(EngineContext &context,
                                                      uint32_t imageCount);
  static std::vector<vk::DescriptorImageInfo>
  makeDescInfos(std::vector<Image2D> &images, vk::Sampler sampler);

  void resizeIntermediateImages(uint32_t imageCount);
  void buildChain(std::vector<ComputePassFactory> &effectFactories);

  bool enabled{true};

  // Own sampler, not context.vulkan.defaultSampler: this is the only
  // place that samples these handoff images, so keeping it local avoids
  // coupling its address-mode/filter choices to whatever the shared
  // default sampler happens to be configured for elsewhere.
  vk::Sampler intermediateSampler;

  // One hand-off image set per stage BOUNDARY (chain.size() - 1 of them,
  // or none for a single-stage chain), each with one image per swapchain
  // image - stage i writes into intermediateImages[i], stage i+1 reads it
  // back. Owned here (not by any stage), since it's purely the plumbing
  // between consecutive stages, not specific to any one effect. Indices
  // parallel intermediateDescInfos.
  std::vector<std::vector<Image2D>> intermediateImages;
  std::vector<std::vector<vk::DescriptorImageInfo>> intermediateDescInfos;

  // Stages, in run order. Each one's concrete PushT/ComputeEffect<PushT>
  // is fully erased behind IComputePass - see ComputePassFactory above.
  std::vector<std::unique_ptr<IComputePass>> chain;
};

} // namespace vkh
