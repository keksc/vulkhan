#include "descriptors.hpp"

#include <cassert>
#include <stdexcept>

namespace vkh {

// *************** Descriptor Set Layout Builder *********************

DescriptorSetLayout::Builder &DescriptorSetLayout::Builder::addBinding(
    uint32_t binding, VkDescriptorType descriptorType,
    VkShaderStageFlags stageFlags, uint32_t count) {
  assert(bindings.count(binding) == 0 && "Binding already in use");
  VkDescriptorSetLayoutBinding layoutBinding{};
  layoutBinding.binding = binding;
  layoutBinding.descriptorType = descriptorType;
  layoutBinding.descriptorCount = count;
  layoutBinding.stageFlags = stageFlags;
  bindings[binding] = layoutBinding;
  return *this;
}

std::unique_ptr<DescriptorSetLayout>
DescriptorSetLayout::Builder::build() const {
  return std::make_unique<DescriptorSetLayout>(context, bindings);
}

// *************** Descriptor Set Layout *********************

DescriptorSetLayout::DescriptorSetLayout(
    EngineContext &context,
    std::unordered_map<uint32_t, VkDescriptorSetLayoutBinding> bindings)
    : context{context}, bindings{bindings} {
  std::vector<VkDescriptorSetLayoutBinding> setLayoutBindings{};
  for (auto &kv : bindings) {
    setLayoutBindings.emplace_back(kv.second);
  }

  VkDescriptorSetLayoutCreateInfo descriptorSetLayoutInfo{};
  descriptorSetLayoutInfo.sType =
      VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
  descriptorSetLayoutInfo.bindingCount =
      static_cast<uint32_t>(setLayoutBindings.size());
  descriptorSetLayoutInfo.pBindings = setLayoutBindings.data();

  if (vkCreateDescriptorSetLayout(context.vulkan.device,
                                  &descriptorSetLayoutInfo, nullptr,
                                  &descriptorSetLayout) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor set layout!");
  }
}

DescriptorSetLayout::~DescriptorSetLayout() {
  vkDestroyDescriptorSetLayout(context.vulkan.device, descriptorSetLayout,
                               nullptr);
}

// *************** Descriptor Pool Builder *********************

DescriptorPool::Builder &
DescriptorPool::Builder::addPoolSize(VkDescriptorType descriptorType,
                                     uint32_t count) {
  poolSizes.emplace_back(descriptorType, count);
  return *this;
}

DescriptorPool::Builder &
DescriptorPool::Builder::setPoolFlags(VkDescriptorPoolCreateFlags flags) {
  poolFlags = flags;
  return *this;
}
DescriptorPool::Builder &DescriptorPool::Builder::setMaxSets(uint32_t count) {
  maxSets = count;
  return *this;
}

std::unique_ptr<DescriptorPool> DescriptorPool::Builder::build() const {
  return std::make_unique<DescriptorPool>(context, maxSets, poolFlags,
                                          poolSizes);
}

// *************** Descriptor Pool *********************

DescriptorPool::DescriptorPool(
    EngineContext &context, uint32_t maxSets,
    VkDescriptorPoolCreateFlags poolFlags,
    const std::vector<VkDescriptorPoolSize> &poolSizes)
    : context{context} {
  VkDescriptorPoolCreateInfo descriptorPoolInfo{};
  descriptorPoolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
  descriptorPoolInfo.poolSizeCount = static_cast<uint32_t>(poolSizes.size());
  descriptorPoolInfo.pPoolSizes = poolSizes.data();
  descriptorPoolInfo.maxSets = maxSets;
  descriptorPoolInfo.flags = poolFlags;

  if (vkCreateDescriptorPool(context.vulkan.device, &descriptorPoolInfo,
                             nullptr, &descriptorPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create descriptor pool!");
  }
}

DescriptorPool::~DescriptorPool() {
  vkDestroyDescriptorPool(context.vulkan.device, descriptorPool, nullptr);
}

bool DescriptorPool::allocateDescriptorSet(
    const VkDescriptorSetLayout descriptorSetLayout,
    VkDescriptorSet &descriptor) const {
  VkDescriptorSetAllocateInfo allocInfo{};
  allocInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
  allocInfo.descriptorPool = descriptorPool;
  allocInfo.pSetLayouts = &descriptorSetLayout;
  allocInfo.descriptorSetCount = 1;

  // Might want to create a "DescriptorPoolManager" class that handles this
  // case, and builds a new pool whenever an old pool fills up. But this is
  // beyond our current scope
  if (vkAllocateDescriptorSets(context.vulkan.device, &allocInfo,
                               &descriptor) != VK_SUCCESS) {
    return false;
  }
  return true;
}

void DescriptorPool::freeDescriptors(
    std::vector<VkDescriptorSet> &descriptors) const {
  vkFreeDescriptorSets(context.vulkan.device, descriptorPool,
                       static_cast<uint32_t>(descriptors.size()),
                       descriptors.data());
}

void DescriptorPool::resetPool() {
  vkResetDescriptorPool(context.vulkan.device, descriptorPool, 0);
}

// *************** Descriptor Writer *********************

DescriptorWriter::DescriptorWriter(DescriptorSetLayout &setLayout,
                                   DescriptorPool &pool)
    : setLayout{setLayout}, pool{pool} {}

DescriptorWriter &
DescriptorWriter::writeBuffer(uint32_t binding,
                              VkDescriptorBufferInfo *bufferInfo) {
  assert(setLayout.bindings.count(binding) == 1 &&
         "Layout does not contain specified binding");

  auto &bindingDescription = setLayout.bindings[binding];

  assert(bindingDescription.descriptorCount == 1 &&
         "Binding single descriptor info, but binding expects multiple");

  writes.emplace_back(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, nullptr,
                      binding, 0, 1, bindingDescription.descriptorType, nullptr,
                      bufferInfo, nullptr);
  return *this;
}

DescriptorWriter &
DescriptorWriter::writeImage(uint32_t binding,
                             VkDescriptorImageInfo *imageInfo) {
  assert(setLayout.bindings.count(binding) == 1 &&
         "Layout does not contain specified binding");

  auto &bindingDescription = setLayout.bindings[binding];

  assert(bindingDescription.descriptorCount == 1 &&
         "Binding single descriptor info, but binding expects multiple");

  writes.emplace_back(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET, nullptr, nullptr,
                      binding, 0, 1, bindingDescription.descriptorType, imageInfo,
                      nullptr, nullptr);
  return *this;
}

bool DescriptorWriter::build(VkDescriptorSet &set) {
  bool success = pool.allocateDescriptorSet(setLayout, set);
  if (!success) {
    return false;
  }
  overwrite(set);
  return true;
}

void DescriptorWriter::overwrite(VkDescriptorSet &set) {
  for (auto &write : writes) {
    write.dstSet = set;
  }
  vkUpdateDescriptorSets(pool.context.vulkan.device,
                         static_cast<uint32_t>(writes.size()), writes.data(), 0,
                         nullptr);
}

} // namespace vkh
