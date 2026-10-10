#pragma once

#include "computePass.hpp"
#include "system.hpp"

#include <functional>
#include <memory>
#include <vector>

namespace vkh {

class Image2D;
class EngineContext;

using ComputePassFactory = std::function<std::unique_ptr<IComputePass>(
    EngineContext &, OutputTargetProvider, InputTargetProvider,
    bool skipInputTransitions)>;

template <typename PushT, typename EffectFactoryFn>
ComputePassFactory makeComputePassFactory(EffectFactoryFn effectFactory) {
  return [effectFactory](
             EngineContext &context, OutputTargetProvider outputTarget,
             InputTargetProvider inputTarget,
             bool skipInputTransitions) -> std::unique_ptr<IComputePass> {
    return std::make_unique<ComputePass<PushT>>(
        context, effectFactory(context), std::move(outputTarget),
        std::move(inputTarget), skipInputTransitions);
  };
}

// A general, ordered chain of postprocessing compute passes, run back to
// back in the same per-frame command buffer.

// Every stage but the last writes into an owned intermediate handoff
// image (see makeIntermediateImages()) instead of the real swapchain
// image, and every stage but the first both skips the shared
// depth/scene-color input barrier (the first stage already transitioned
// them once this frame - see ComputePass's skipInputTransitions doc
// comment) AND samples the previous stage's hand-off image instead of
// the real resolved scene color as its binding-1 input (see
// ComputePass's InputTargetProvider) - that's what makes this an actual
// chain rather than several stages independently reprocessing the same
// original scene color. The last stage writes the real swapchain image
// and owns the present-layout transition, exactly as a single, unchained
// ComputePass always has. A chain of one stage gets no intermediate image
// at all - that one stage writes the swapchain image directly, same as
// before any chaining existed.
class PostProcessingSys : public System {
public:
  // effectFactories must be non-empty - the last stage owns the real
  // swapchain image and its present-layout transition (see run()), so
  // there's no meaningful zero-stage chain. Order is run order.
  PostProcessingSys(EngineContext &context,
                    std::vector<ComputePassFactory> effectFactories);
  ~PostProcessingSys();

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
