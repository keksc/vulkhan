#pragma once

#include <vulkan/vulkan_core.h>

#include "engineContext.hpp"

#include <deque>
#include <span>
#include <vector>

namespace vkh {

class DescriptorAllocatorGrowable {
public:
  struct PoolSizeRatio {
    VkDescriptorType type;
    float ratio;
  };

  DescriptorAllocatorGrowable(EngineContext &context);
  ~DescriptorAllocatorGrowable();
  void init(uint32_t initialSets, std::span<PoolSizeRatio> poolRatios);
  void clearPools();
  void destroyPools();

  VkDescriptorSet allocate(VkDescriptorSetLayout layout, void *pNext = nullptr);

private:
  VkDescriptorPool getPool();
  VkDescriptorPool createPool(uint32_t setCount,
                              std::span<PoolSizeRatio> poolRatios);

  std::vector<PoolSizeRatio> ratios;
  std::vector<VkDescriptorPool> fullPools;
  std::vector<VkDescriptorPool> readyPools;
  uint32_t setsPerPool;

  EngineContext &context;
};
class DescriptorWriter {
public:
  DescriptorWriter(EngineContext &context);

  void writeImage(int binding, VkDescriptorImageInfo imageInfo,
                  VkDescriptorType type);
  void writeBuffer(int binding, VkDescriptorBufferInfo bufferInfo,
                   VkDescriptorType type);

  void clear();
  void updateSet(VkDescriptorSet set);

private:
  std::deque<VkDescriptorImageInfo> imageInfos;
  std::deque<VkDescriptorBufferInfo> bufferInfos;
  std::vector<VkWriteDescriptorSet> writes;

  EngineContext &context;
};
VkDescriptorSetLayout buildDescriptorSetLayout(
    EngineContext &context, std::vector<VkDescriptorSetLayoutBinding> bindings,
    VkDescriptorSetLayoutCreateFlags flags = 0, void *pNext = nullptr);
} // namespace vkh
