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

  void run(vk::CommandBuffer cmd, uint32_t imageIndex);
  void recreateDescriptors();

private:
  void createDescriptors();
  void createPipeline();

  std::unique_ptr<ComputePipeline> pipeline;
  vk::DescriptorSetLayout setLayout;
  std::vector<vk::DescriptorSet> descriptorSets;

  SwapChain *savedSwapChain{};
};

} // namespace vkh
