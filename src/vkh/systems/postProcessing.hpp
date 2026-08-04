#pragma once

#include <vulkan/vulkan.hpp>

#include "../swapChain.hpp"
#include "system.hpp"

#include <memory>
#include <vector>

namespace vkh {

class ComputePipeline;

class PostProcessingSys : public System {
public:
  PostProcessingSys(EngineContext &context);
  ~PostProcessingSys();

  // If postprocessing is disabled, run() falls back to a plain copy of the
  // resolved scene color straight into the swapchain image, so the frame
  // still presents correctly even when the compute pass is skipped.
  void setEnabled(bool value) { enabled = value; }
  bool isEnabled() const { return enabled; }

  void run(vk::CommandBuffer cmd, uint32_t imageIndex);
  void recreateDescriptors();

private:
  void createDescriptors();
  void allocateAndWriteDescriptorSets();
  void createPipeline();
  void runPassthrough(vk::CommandBuffer cmd, uint32_t imageIndex);

  bool enabled{true};

  std::unique_ptr<ComputePipeline> pipeline;
  vk::DescriptorSetLayout setLayout;
  std::vector<vk::DescriptorSet> descriptorSets;

  SwapChain *savedSwapChain{};
};

} // namespace vkh
