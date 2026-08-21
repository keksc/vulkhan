#pragma once

#include <vulkan/vulkan.hpp>

#include "../swapChain.hpp"
#include "smoke/fluidGrid.hpp"
#include "system.hpp"

#include <memory>
#include <vector>

namespace vkh {

class ComputePipeline;

class PostProcessingSys : public System {
public:
  // smokeGrid's dye volume is sampled directly by the compute shader here
  // (see the dye-volume binding in createDescriptors()) - smoke rendering
  // lives in this pass rather than its own, since a dedicated graphics
  // pass sampling the resolved depth image while depth is also bound as
  // that pass's attachment is a feedback loop; this compute pass reads
  // depth cleanly with no such conflict.
  PostProcessingSys(EngineContext &context, FluidGrid &smokeGrid);
  ~PostProcessingSys();

  // If postprocessing is disabled, run() falls back to a plain copy of the
  // resolved scene color straight into the swapchain image, so the frame
  // still presents correctly even when the compute pass is skipped. NOTE:
  // this also means smoke doesn't render while disabled, since smoke
  // compositing now lives inside the compute pass - runPassthrough() has
  // no equivalent of it.
  void setEnabled(bool value) { enabled = value; }
  bool isEnabled() const { return enabled; }

  void run(uint32_t imageIndex);
  void recreateDescriptors();

private:
  void createDescriptors();
  void allocateAndWriteDescriptorSets();
  void createPipeline();
  void runPassthrough(uint32_t imageIndex);

  bool enabled{true};

  FluidGrid &smokeGrid;
  // Dedicated sampler for the dye volume: it's periodic (see FluidGrid's
  // localIndex - a fixed wrap(worldCell, cellCount) with no origin
  // offset), so it needs REPEAT addressing on all three axes. Using
  // context.vulkan.defaultSampler (presumably clamp-to-edge, like the
  // other bindings here) would smear near a wrapped edge instead of
  // continuing across it.
  vk::Sampler smokeVolumeSampler;

  // Matches SmokePush in postProcessing.comp.
  struct SmokePush {
    glm::vec4 boxMin;    // xyz = gridOrigin * cellSize, w = cellSize
    glm::ivec4 cellCount; // xyz used
  };

  std::unique_ptr<ComputePipeline> pipeline;
  vk::DescriptorSetLayout setLayout;
  std::vector<vk::DescriptorSet> descriptorSets;

  SwapChain *savedSwapChain{};
};

} // namespace vkh
