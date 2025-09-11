#pragma once

#include <vulkan/vulkan_core.h>

#include "../swapChain.hpp"
#include "system.hpp"

#include <vector>

namespace vkh {
class PostProcessingSys : public System {
public:
  PostProcessingSys(EngineContext &context);

  void run(VkCommandBuffer cmd, uint32_t imageIndex);
  void recreateDescriptors();

private:
  void createDescriptors();
  void createPipeline();

  std::unique_ptr<ComputePipeline> pipeline;
  std::unique_ptr<DescriptorSetLayout> setLayout;
  std::vector<VkDescriptorSet> descriptorSets;

  SwapChain *savedSwapChain{};
};
} // namespace vkh
