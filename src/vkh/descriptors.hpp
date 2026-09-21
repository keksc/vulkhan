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

  // Writes a single binding as a descriptor ARRAY (descriptorCount =
  // imageInfos.size()) instead of a scalar descriptor - for a
  // descriptor-indexed binding declared with an array size > 1 in the
  // shader (e.g. `uniform sampler3D transmittanceVolumes[N]`). imageInfos
  // may hold fewer entries than the binding's full array size when the
  // binding was created with VK_DESCRIPTOR_BINDING_PARTIALLY_BOUND_BIT -
  // the remaining slots simply aren't written and must not be read by
  // the shader past whatever count it's told separately (e.g. via push
  // constant).
  void writeImageArray(uint32_t binding,
                       const std::vector<vk::DescriptorImageInfo> &imageInfos,
                       vk::DescriptorType type);

  void clear();
  void updateSet(vk::DescriptorSet set);

private:
  std::deque<vk::DescriptorImageInfo> imageInfos;
  std::deque<vk::DescriptorBufferInfo> bufferInfos;
  // Own container for array writes: unlike the single-image case above,
  // an array write's vk::WriteDescriptorSet::pImageInfo must point at N
  // CONTIGUOUS entries, which a std::deque<vk::DescriptorImageInfo> can't
  // back (stable per-element addresses, but no contiguity across
  // elements). A std::deque of vectors keeps each vector's own buffer
  // contiguous and its address stable (the deque never moves/reallocates
  // existing elements on push_back), so pointers into it stay valid until
  // clear().
  std::deque<std::vector<vk::DescriptorImageInfo>> imageInfoArrays;
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
