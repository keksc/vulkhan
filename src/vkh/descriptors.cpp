#include "descriptors.hpp"
#include "debug.hpp"

#include <format>
#include <stdexcept>
#include <vulkan/vulkan.hpp>

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

vk::DescriptorPool DescriptorAllocatorGrowable::getPool() {
  vk::DescriptorPool newPool;
  if (!readyPools.empty()) {
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

vk::DescriptorPool
DescriptorAllocatorGrowable::createPool(uint32_t setCount,
                                        std::span<PoolSizeRatio> poolRatios) {
  std::vector<vk::DescriptorPoolSize> poolSizes;
  for (auto &ratio : poolRatios) {
    poolSizes.emplace_back(ratio.type,
                           static_cast<uint32_t>(ratio.ratio * setCount));
  }

  vk::DescriptorPoolCreateInfo poolInfo{};
  poolInfo.maxSets = setCount;
  poolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  poolInfo.pPoolSizes = poolSizes.data();

  vk::DescriptorPool newPool =
      context.vulkan.device.createDescriptorPool(poolInfo);

  std::string str =
      std::format("pool #{}", readyPools.size() + fullPools.size());
  debug::setObjName(
      context, vk::ObjectType::eDescriptorPool,
      reinterpret_cast<uint64_t>(static_cast<VkDescriptorPool>(newPool)),
      str.c_str());
  return newPool;
}

void DescriptorAllocatorGrowable::clearPools() {
  for (auto &p : readyPools) {
    context.vulkan.device.resetDescriptorPool(p);
  }
  for (auto &p : fullPools) {
    context.vulkan.device.resetDescriptorPool(p);
    readyPools.push_back(p);
  }
  fullPools.clear();
}

void DescriptorAllocatorGrowable::destroyPools() {
  for (auto &p : readyPools) {
    context.vulkan.device.destroyDescriptorPool(p, nullptr);
  }
  readyPools.clear();
  for (auto &p : fullPools) {
    context.vulkan.device.destroyDescriptorPool(p, nullptr);
  }
  fullPools.clear();
}

vk::DescriptorSet
DescriptorAllocatorGrowable::allocate(vk::DescriptorSetLayout layout,
                                      void *pNext) {
  vk::DescriptorPool poolToUse = getPool();

  vk::DescriptorSetAllocateInfo allocInfo{};
  allocInfo.pNext = pNext;
  allocInfo.descriptorPool = poolToUse;
  allocInfo.descriptorSetCount = 1;
  allocInfo.pSetLayouts = &layout;

  vk::DescriptorSet set;
  try {
    // Explicitly using the result variant to mimic your C-style result check
    // flow safely
    vk::Result result =
        context.vulkan.device.allocateDescriptorSets(&allocInfo, &set);
    if (result == vk::Result::eErrorOutOfPoolMemory ||
        result == vk::Result::eErrorFragmentedPool) {
      fullPools.push_back(poolToUse);
      poolToUse = getPool();
      allocInfo.descriptorPool = poolToUse;
      if (context.vulkan.device.allocateDescriptorSets(&allocInfo, &set) !=
          vk::Result::eSuccess)
        throw std::runtime_error("could not allocate descriptor pool");
    }
  } catch (const vk::SystemError &) {
    fullPools.push_back(poolToUse);
    poolToUse = getPool();
    allocInfo.descriptorPool = poolToUse;
    if (context.vulkan.device.allocateDescriptorSets(&allocInfo, &set) !=
        vk::Result::eSuccess)
      throw std::runtime_error("could not allocate descriptor pool");
  }

  readyPools.push_back(poolToUse);
  return set;
}

DescriptorWriter::DescriptorWriter(EngineContext &context) : context{context} {}

void DescriptorWriter::writeBuffer(uint32_t binding,
                                   vk::DescriptorBufferInfo bufferInfo,
                                   vk::DescriptorType type) {
  vk::DescriptorBufferInfo &info = bufferInfos.emplace_back(bufferInfo);

  vk::WriteDescriptorSet write{};
  write.dstSet = nullptr;
  write.dstBinding = binding;
  write.descriptorCount = 1;
  write.descriptorType = type;
  write.pBufferInfo = &info;

  writes.push_back(write);
}

void DescriptorWriter::writeImage(uint32_t binding,
                                  vk::DescriptorImageInfo imageInfo,
                                  vk::DescriptorType type) {
  vk::DescriptorImageInfo &info = imageInfos.emplace_back(imageInfo);

  vk::WriteDescriptorSet write{};
  write.dstSet = nullptr;
  write.dstBinding = binding;
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

void DescriptorWriter::updateSet(vk::DescriptorSet set) {
  for (vk::WriteDescriptorSet &write : writes) {
    write.dstSet = set;
  }

  context.vulkan.device.updateDescriptorSets(
      static_cast<uint32_t>(writes.size()), writes.data(), 0, nullptr);
}

vk::DescriptorSetLayout buildDescriptorSetLayout(
    EngineContext &context,
    const std::vector<vk::DescriptorSetLayoutBinding> &bindings,
    vk::DescriptorSetLayoutCreateFlags flags, void *pNext) {
  vk::DescriptorSetLayoutCreateInfo info{};
  info.bindingCount = static_cast<uint32_t>(bindings.size());
  info.pBindings = bindings.data();
  info.flags = flags;
  info.pNext = pNext;

  return context.vulkan.device.createDescriptorSetLayout(info);
}

} // namespace vkh
