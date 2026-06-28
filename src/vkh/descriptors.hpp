#pragma once

#include <vulkan/vulkan.hpp>

#include "engineContext.hpp"

#include <deque>
#include <span>
#include <vector>

namespace vkh {

class DescriptorAllocatorGrowable {
public:
  struct PoolSizeRatio {
    vk::DescriptorType type;
    float ratio;
  };

  DescriptorAllocatorGrowable(EngineContext &context);
  ~DescriptorAllocatorGrowable();
  void init(uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
  void clearPools();
  void destroyPools();

  vk::DescriptorSet allocate(vk::DescriptorSetLayout layout,
                             void *pNext = nullptr);

private:
  vk::DescriptorPool getPool();
  vk::DescriptorPool createPool(uint32_t setCount,
                                std::span<PoolSizeRatio> poolRatios);

  std::vector<PoolSizeRatio> ratios;
  std::vector<vk::DescriptorPool> fullPools;
  std::vector<vk::DescriptorPool> readyPools;
  uint32_t setsPerPool;

  EngineContext &context;
};

class DescriptorWriter {
public:
  DescriptorWriter(EngineContext &context);

  void writeImage(uint32_t binding, vk::DescriptorImageInfo imageInfo,
                  vk::DescriptorType type);
  void writeBuffer(uint32_t binding, vk::DescriptorBufferInfo bufferInfo,
                   vk::DescriptorType type);

  void clear();
  void updateSet(vk::DescriptorSet set);

private:
  std::deque<vk::DescriptorImageInfo> imageInfos;
  std::deque<vk::DescriptorBufferInfo> bufferInfos;
  std::vector<vk::WriteDescriptorSet> writes;

  EngineContext &context;
};

vk::DescriptorSetLayout buildDescriptorSetLayout(
    EngineContext &context,
    const std::vector<vk::DescriptorSetLayoutBinding> &bindings,
    vk::DescriptorSetLayoutCreateFlags flags =
        vk::DescriptorSetLayoutCreateFlags{},
    void *pNext = nullptr);

} // namespace vkh
