#include "descriptors.hpp"
#include "debug.hpp"

#include <format>
#include <stdexcept>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

namespace vkh {
DescriptorAllocatorGrowable::DescriptorAllocatorGrowable(EngineContext &context)
    : context{context} {}
DescriptorAllocatorGrowable::~DescriptorAllocatorGrowable() { destroyPools(); }
void DescriptorAllocatorGrowable::init(uint32_t initialSets,
                                       std::span<PoolSizeRatio> poolRatios) {
  ratios.clear();
  for (auto &r : poolRatios)
    ratios.emplace_back(r);
  setsPerPool = initialSets * 1.5f;
  readyPools.push_back(createPool(initialSets, poolRatios));
}
VkDescriptorPool DescriptorAllocatorGrowable::getPool() {
  VkDescriptorPool newPool;
  if (readyPools.size() > 0) {
    newPool = readyPools.back();
    readyPools.pop_back();
  } else {
    newPool = createPool(setsPerPool, ratios);
    setsPerPool = setsPerPool * 1.5f;
    if (setsPerPool > 4092)
      setsPerPool = 4092;
  }
  return newPool;
}
VkDescriptorPool
DescriptorAllocatorGrowable::createPool(uint32_t setCount,
                                        std::span<PoolSizeRatio> poolRatios) {
  std::vector<VkDescriptorPoolSize> poolSizes;
  for (auto &ratio : poolRatios) {
    poolSizes.emplace_back(ratio.type,
                           static_cast<uint32_t>(ratio.ratio * setCount));
  }
  VkDescriptorPoolCreateInfo poolInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
  poolInfo.maxSets = setCount;
  poolInfo.poolSizeCount = poolSizes.size();
  poolInfo.pPoolSizes = poolSizes.data();

  VkDescriptorPool newPool;
  vkCreateDescriptorPool(context.vulkan.device, &poolInfo, nullptr, &newPool);
  std::string str =
      std::format("pool #{}", readyPools.size() + fullPools.size());
  debug::setObjName(context, VK_OBJECT_TYPE_DESCRIPTOR_POOL,
                    reinterpret_cast<uint64_t>(newPool), str.c_str());
  return newPool;
}
void DescriptorAllocatorGrowable::clearPools() {
  for (auto &p : readyPools) {
    vkResetDescriptorPool(context.vulkan.device, p, 0);
  }
  for (auto &p : fullPools) {
    vkResetDescriptorPool(context.vulkan.device, p, 0);
    readyPools.push_back(p);
  }
  fullPools.clear();
}
void DescriptorAllocatorGrowable::destroyPools() {
  for (auto &p : readyPools) {
    vkDestroyDescriptorPool(context.vulkan.device, p, nullptr);
  }
  readyPools.clear();
  for (auto &p : fullPools) {
    vkDestroyDescriptorPool(context.vulkan.device, p, nullptr);
  }
  fullPools.clear();
}
VkDescriptorSet
DescriptorAllocatorGrowable::allocate(VkDescriptorSetLayout layout,
                                      void *pNext) {
  VkDescriptorPool poolToUse = getPool();
  VkDescriptorSetAllocateInfo allocInfo{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
  allocInfo.pNext = pNext;
  allocInfo.descriptorPool = poolToUse;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  VkDescriptorSet set;
  VkResult result =
      vkAllocateDescriptorSets(context.vulkan.device, &allocInfo, &set);
  if (result == VK_ERROR_OUT_OF_POOL_MEMORY ||
      result == VK_ERROR_FRAGMENTED_POOL) {
    fullPools.push_back(poolToUse);
    poolToUse = getPool();
    allocInfo.descriptorPool = poolToUse;
    if (vkAllocateDescriptorSets(context.vulkan.device, &allocInfo, &set) !=
        VK_SUCCESS)
      throw std::runtime_error("could not allocate descriptor pool");
  }
  readyPools.push_back(poolToUse);
  return set;
}
DescriptorWriter::DescriptorWriter(EngineContext &context) : context{context} {}
void DescriptorWriter::writeBuffer(int binding,
                                   VkDescriptorBufferInfo bufferInfo,
                                   VkDescriptorType type) {
  VkDescriptorBufferInfo &info = bufferInfos.emplace_back(bufferInfo);

  VkWriteDescriptorSet write = {.sType =
                                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

  write.dstBinding = binding;
  write.dstSet = VK_NULL_HANDLE; // left empty for now until we need to write it
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pBufferInfo = &info;

  writes.push_back(write);
}
void DescriptorWriter::writeImage(int binding, VkDescriptorImageInfo imageInfo,
                                  VkDescriptorType type) {
  VkDescriptorImageInfo &info = imageInfos.emplace_back(imageInfo);

  VkWriteDescriptorSet write = {.sType =
                                    VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET};

  write.dstBinding = binding;
  write.dstSet = VK_NULL_HANDLE; // left empty for now until we need to write it
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pImageInfo = &info;

  writes.push_back(write);
}
void DescriptorWriter::clear() {
  imageInfos.clear();
  writes.clear();
  bufferInfos.clear();
}

void DescriptorWriter::updateSet(VkDescriptorSet set) {
  for (VkWriteDescriptorSet &write : writes) {
    write.dstSet = set;
  }

  vkUpdateDescriptorSets(context.vulkan.device, (uint32_t)writes.size(),
                         writes.data(), 0, nullptr);
}
VkDescriptorSetLayout
buildDescriptorSetLayout(EngineContext &context,
                         std::vector<VkDescriptorSetLayoutBinding> bindings,
                         VkDescriptorSetLayoutCreateFlags flags, void *pNext) {
  VkDescriptorSetLayoutCreateInfo info{
      .sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
  info.bindingCount = bindings.size();
  info.pBindings = bindings.data();
  info.flags = flags;
  info.pNext = pNext;

  VkDescriptorSetLayout setLayout;
  vkCreateDescriptorSetLayout(context.vulkan.device, &info, nullptr,
                              &setLayout);
  return setLayout;
}
} // namespace vkh
