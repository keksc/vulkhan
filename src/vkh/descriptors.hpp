#pragma once

#include "engineContext.hpp"

#include <memory>
#include <unordered_map>
#include <vector>

namespace vkh {

class DescriptorSetLayout {
public:
  class Builder {
  public:
    Builder(EngineContext &context) : context{context} {}

    Builder &addBinding(uint32_t binding, VkDescriptorType descriptorType,
                        VkShaderStageFlags stageFlags, uint32_t count = 1);
    std::unique_ptr<DescriptorSetLayout> build() const;

  private:
    EngineContext &context;
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings{};
  };

  DescriptorSetLayout(
      EngineContext &context,
      std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings);
  ~DescriptorSetLayout();
  DescriptorSetLayout(const DescriptorSetLayout &) = delete;
  DescriptorSetLayout &operator=(const DescriptorSetLayout &) = delete;

  operator VkDescriptorSetLayout() { return descriptorSetLayout; }
  operator VkDescriptorSetLayout *() { return &descriptorSetLayout; }

private:
  EngineContext &context;
  VkDescriptorSetLayout descriptorSetLayout;
  std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings;

  friend class DescriptorWriter;
};

class DescriptorPool {
public:
  class Builder {
  public:
    Builder(EngineContext &context) : context{context} {}

    Builder &addPoolSize(VkDescriptorType descriptorType, uint32_t count);
    Builder &setPoolFlags(VkDescriptorPoolCreateFlags flags);
    Builder &setMaxSets(uint32_t count);
    std::unique_ptr<DescriptorPool> build() const;

  private:
    EngineContext &context;
    std::vector<VkDescriptorPoolSize> poolSizes{};
    uint32_t maxSets = 1000;
    VkDescriptorPoolCreateFlags poolFlags = 0;
  };

  DescriptorPool(EngineContext &context, uint32_t maxSets,
                 VkDescriptorPoolCreateFlags poolFlags,
                 const std::vector<VkDescriptorPoolSize> &poolSizes);
  ~DescriptorPool();
  DescriptorPool(const DescriptorPool &) = delete;
  DescriptorPool &operator=(const DescriptorPool &) = delete;

  bool allocateDescriptorSet(const VkDescriptorSetLayout descriptorSetLayout,
                             VkDescriptorSet &descriptor) const;

  void freeDescriptors(std::vector<VkDescriptorSet> &descriptors) const;

  void resetPool();

private:
  EngineContext &context;
  VkDescriptorPool descriptorPool;

  friend class DescriptorWriter;
};

class DescriptorWriter {
public:
  DescriptorWriter(DescriptorSetLayout &setLayout, DescriptorPool &pool);

  DescriptorWriter &writeBuffer(uint32_t binding,
                                VkDescriptorBufferInfo *bufferInfo);
  DescriptorWriter &writeImage(uint32_t binding,
                               VkDescriptorImageInfo *imageInfo);

  bool build(VkDescriptorSet &set);
  void overwrite(VkDescriptorSet &set);

private:
  DescriptorSetLayout &setLayout;
  DescriptorPool &pool;
  std::vector<VkWriteDescriptorSet> writes;
};

} // namespace vkh
