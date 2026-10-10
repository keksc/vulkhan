#include "../src/vkh/systems/culling.hpp"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <fstream>
#include <iostream>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace {
int validationErrors = 0;
VKAPI_ATTR VkBool32 VKAPI_CALL
onValidation(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
             VkDebugUtilsMessageTypeFlagsEXT,
             const VkDebugUtilsMessengerCallbackDataEXT *data, void *) {
  if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
    ++validationErrors;
    std::cerr << data->pMessage << '\n';
  }
  return VK_FALSE;
}

struct Buffer {
  vk::UniqueDeviceMemory memory;
  vk::UniqueBuffer buffer;
  void *mapped;
};

std::vector<uint32_t> spirv(const std::string &file) {
  std::ifstream stream(file, std::ios::binary | std::ios::ate);
  if (!stream)
    throw std::runtime_error("Cannot read " + file);
  const auto bytes = stream.tellg();
  std::vector<uint32_t> code(size_t(bytes) / 4);
  stream.seekg(0);
  stream.read(reinterpret_cast<char *>(code.data()), bytes);
  return code;
}
} // namespace

int main(int argc, char **argv) {
  const std::string shaderRoot = argc > 1 ? argv[1] : "build/shaders";
  const auto layers = vk::enumerateInstanceLayerProperties();
  const bool validation = std::any_of(layers.begin(), layers.end(), [](auto l) {
    return std::strcmp(l.layerName, "VK_LAYER_KHRONOS_validation") == 0;
  });
  const char *layer = "VK_LAYER_KHRONOS_validation";
  const char *extension = VK_EXT_DEBUG_UTILS_EXTENSION_NAME;
  vk::ApplicationInfo application("culling regression", 0, "test", 0,
                                  VK_API_VERSION_1_2);
  vk::InstanceCreateInfo info{};
  info.pApplicationInfo = &application;
  info.enabledExtensionCount = 1;
  info.ppEnabledExtensionNames = &extension;
  const vk::ValidationFeatureEnableEXT sync =
      vk::ValidationFeatureEnableEXT::eSynchronizationValidation;
  vk::ValidationFeaturesEXT features{};
  features.enabledValidationFeatureCount = 1;
  features.pEnabledValidationFeatures = &sync;
  if (validation) {
    info.enabledLayerCount = 1;
    info.ppEnabledLayerNames = &layer;
    info.pNext = &features;
  }
  auto instance = vk::createInstanceUnique(info);
  const auto createMessenger =
      reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          instance->getProcAddr("vkCreateDebugUtilsMessengerEXT"));
  const auto destroyMessenger =
      reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(
          instance->getProcAddr("vkDestroyDebugUtilsMessengerEXT"));
  VkDebugUtilsMessengerCreateInfoEXT messengerInfo{
      VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT;
  messengerInfo.pfnUserCallback = onValidation;
  VkDebugUtilsMessengerEXT messenger{};
  assert(createMessenger(*instance, &messengerInfo, nullptr, &messenger) ==
         VK_SUCCESS);
  {
    auto physical = instance->enumeratePhysicalDevices().at(0);
    const auto families = physical.getQueueFamilyProperties();
    uint32_t family = 0;
    const auto required =
        vk::QueueFlagBits::eCompute | vk::QueueFlagBits::eGraphics;
    while (family < families.size() &&
           (families[family].queueFlags & required) != required)
      ++family;
    assert(family < families.size());
    const float priority = 1;
    vk::DeviceQueueCreateInfo queueInfo({}, family, 1, &priority);
    auto device =
        physical.createDeviceUnique(vk::DeviceCreateInfo({}, queueInfo));
    const auto queue = device->getQueue(family, 0);
    const auto memoryProperties = physical.getMemoryProperties();
    auto makeBuffer = [&](size_t bytes) {
      auto buffer = std::make_unique<Buffer>();
      buffer->buffer = device->createBufferUnique(
          vk::BufferCreateInfo({}, bytes,
                               vk::BufferUsageFlagBits::eStorageBuffer |
                                   vk::BufferUsageFlagBits::eTransferSrc |
                                   vk::BufferUsageFlagBits::eTransferDst |
                                   vk::BufferUsageFlagBits::eIndirectBuffer));
      const auto requirements =
          device->getBufferMemoryRequirements(*buffer->buffer);
      uint32_t type = 0;
      const auto flags = vk::MemoryPropertyFlagBits::eHostVisible |
                         vk::MemoryPropertyFlagBits::eHostCoherent;
      while (
          type < memoryProperties.memoryTypeCount &&
          (!(requirements.memoryTypeBits & (1u << type)) ||
           (memoryProperties.memoryTypes[type].propertyFlags & flags) != flags))
        ++type;
      assert(type < memoryProperties.memoryTypeCount);
      buffer->memory = device->allocateMemoryUnique(
          vk::MemoryAllocateInfo(requirements.size, type));
      device->bindBufferMemory(*buffer->buffer, *buffer->memory, 0);
      buffer->mapped = device->mapMemory(*buffer->memory, 0, bytes);
      std::memset(buffer->mapped, 0, bytes);
      return buffer;
    };
    const auto stage = vk::ShaderStageFlagBits::eCompute;
    std::array<vk::DescriptorSetLayoutBinding, 6> bindings{};
    for (uint32_t i = 0; i < bindings.size(); ++i)
      bindings[i] = {i + 1, vk::DescriptorType::eStorageBuffer, 1, stage};
    auto descriptors = device->createDescriptorSetLayoutUnique(
        vk::DescriptorSetLayoutCreateInfo({}, bindings));
    const vk::PushConstantRange range(stage, 0, sizeof(vkh::CullingPush));
    auto layout = device->createPipelineLayoutUnique(
        vk::PipelineLayoutCreateInfo({}, *descriptors, range));
    const vk::DescriptorPoolSize poolSize(vk::DescriptorType::eStorageBuffer,
                                          6);
    auto pool = device->createDescriptorPoolUnique(
        vk::DescriptorPoolCreateInfo({}, 1, poolSize));
    const auto set = device
                         ->allocateDescriptorSets(
                             vk::DescriptorSetAllocateInfo(*pool, *descriptors))
                         .front();
    auto pipeline = [&](const std::string &path) {
      const auto code = spirv(shaderRoot + '/' + path);
      auto module = device->createShaderModuleUnique(
          vk::ShaderModuleCreateInfo({}, code.size() * 4, code.data()));
      return device
          ->createComputePipelineUnique(
              {},
              vk::ComputePipelineCreateInfo(
                  {},
                  vk::PipelineShaderStageCreateInfo({}, stage, *module, "main"),
                  *layout))
          .value;
    };
    auto commandPool =
        device->createCommandPoolUnique(vk::CommandPoolCreateInfo(
            vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family));
    auto commands =
        device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
            *commandPool, vk::CommandBufferLevel::ePrimary, 1));
    const auto cmd = *commands.front();
    // Both shader instance structures have a 192-byte stride. AABB and group
    // offsets differ; encode them explicitly to exercise both real shaders.
    for (bool trees : {false, true}) {
      auto raw = makeBuffer(4 * 192), compacted = makeBuffer(4 * 192);
      auto first = makeBuffer(8), counts = makeBuffer(8),
           groups = makeBuffer(12);
      auto indirect = makeBuffer(3 * sizeof(vk::DrawIndexedIndirectCommand));
      constexpr size_t snapshotBytes =
          3 * sizeof(vk::DrawIndexedIndirectCommand) + 4 * 192;
      auto snapshots = makeBuffer(3 * snapshotBytes);
      const std::array<Buffer *, 6> buffers{raw.get(),    compacted.get(),
                                            first.get(),  counts.get(),
                                            groups.get(), indirect.get()};
      for (uint32_t i = 0; i < buffers.size(); ++i) {
        const vk::DescriptorBufferInfo bufferInfo(*buffers[i]->buffer, 0,
                                                  VK_WHOLE_SIZE);
        device->updateDescriptorSets(
            vk::WriteDescriptorSet(set, i + 1, 0, 1,
                                   vk::DescriptorType::eStorageBuffer, nullptr,
                                   &bufferInfo),
            {});
      }
      for (uint32_t i = 0; i < 4; ++i) {
        auto *words = static_cast<uint32_t *>(raw->mapped) + i * 48;
        auto *values = reinterpret_cast<float *>(words);
        const float x = i < 2 ? -4.f + i : float(i + 1);
        values[12] = float(i); // retained instance ID in model matrix
        const int lower = trees ? 32 : 36, upper = trees ? 36 : 40;
        values[lower] = x - .1f;
        values[upper] = x + .1f;
        values[lower + 1] = values[lower + 2] = -.1f;
        values[upper + 1] = values[upper + 2] = .1f;
        words[trees ? 44 : 47] = i / 2;
      }
      const std::array<uint32_t, 2> starts{0, 2};
      const std::array<uint32_t, 3> commandGroups{0, 0, 1};
      std::memcpy(first->mapped, starts.data(), sizeof(starts));
      std::memcpy(groups->mapped, commandGroups.data(), sizeof(commandGroups));
      std::array<vk::DrawIndexedIndirectCommand, 3> draws{
          vk::DrawIndexedIndirectCommand(3, 0, 0, 0, 0),
          vk::DrawIndexedIndirectCommand(6, 0, 3, 0, 0),
          vk::DrawIndexedIndirectCommand(3, 0, 9, 0, 2)};
      std::memcpy(indirect->mapped, draws.data(), sizeof(draws));
      auto cull = pipeline(trees ? "tree/treeCulling.comp.spv"
                                 : "entity/culling.comp.spv");
      auto finalize = pipeline(trees ? "tree/treeCullingFinalize.comp.spv"
                                     : "entity/cullingFinalize.comp.spv");
      cmd.reset();
      cmd.begin(vk::CommandBufferBeginInfo(
          vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
      vk::MemoryBarrier host(vk::AccessFlagBits::eHostWrite,
                             vk::AccessFlagBits::eShaderRead);
      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                          vk::PipelineStageFlagBits::eComputeShader, {}, host,
                          nullptr, nullptr);
      for (int viewIndex = 0; viewIndex < 3; ++viewIndex) {
        // The snapshots are transfer consumers; wait for their reads before
        // allowing the production helper to reuse the shared output buffers.
        vk::MemoryBarrier consumed(vk::AccessFlagBits::eTransferRead,
                                   vk::AccessFlagBits::eShaderWrite);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                            vk::PipelineStageFlagBits::eComputeShader, {},
                            consumed, nullptr, nullptr);
        vkh::CullingPush view{};
        for (auto &plane : view.frustumPlanes)
          plane = {0, 0, 0, 1};
        view.frustumPlanes[0] = {viewIndex == 0 ? -1.f : 1.f, 0, 0, 0};
        view.totalInstances = viewIndex == 2 ? 0 : 4;
        view.totalCommands = 3;
        vkh::recordCulling(cmd, view,
                           {*cull, *finalize, *layout, set, *compacted->buffer,
                            *indirect->buffer, *counts->buffer, 2});
        vk::MemoryBarrier ready(vk::AccessFlagBits::eShaderWrite,
                                vk::AccessFlagBits::eTransferRead);
        cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                            vk::PipelineStageFlagBits::eTransfer, {}, ready,
                            nullptr, nullptr);
        cmd.copyBuffer(
            *indirect->buffer, *snapshots->buffer,
            vk::BufferCopy(0, viewIndex * snapshotBytes, sizeof(draws)));
        cmd.copyBuffer(*compacted->buffer, *snapshots->buffer,
                       vk::BufferCopy(0,
                                      viewIndex * snapshotBytes + sizeof(draws),
                                      4 * 192));
      }
      vk::MemoryBarrier ready(vk::AccessFlagBits::eTransferWrite,
                              vk::AccessFlagBits::eHostRead);
      cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                          vk::PipelineStageFlagBits::eHost, {}, ready, nullptr,
                          nullptr);
      cmd.end();
      queue.submit(vk::SubmitInfo({}, {}, cmd), {});
      queue.waitIdle();
      for (int v = 0; v < 3; ++v) {
        const auto *bytes =
            static_cast<const char *>(snapshots->mapped) + v * snapshotBytes;
        std::array<vk::DrawIndexedIndirectCommand, 3> result;
        std::memcpy(result.data(), bytes, sizeof(result));
        for (int i = 0; i < 3; ++i) {
          assert(result[i].instanceCount ==
                 (v == 0 ? (i < 2 ? 2u : 0u) : (v == 1 && i == 2 ? 2u : 0u)));
          assert(result[i].indexCount == draws[i].indexCount);
          assert(result[i].firstInstance == draws[i].firstInstance);
        }
        if (v < 2) {
          std::set<int> ids;
          for (int i = 0; i < 2; ++i) {
            float id;
            std::memcpy(&id, bytes + sizeof(draws) + (v * 2 + i) * 192 + 12 * 4,
                        4);
            ids.insert(int(id));
          }
          assert(ids == (v == 0 ? std::set<int>{0, 1} : std::set<int>{2, 3}));
        }
      }
      std::cout << (trees ? "Tree" : "Entity")
                << " culling: distinct views, shared primitive groups, empty "
                   "view passed\n";
    }
    device->waitIdle();
    std::cout << "Driver: " << physical.getProperties().deviceName
              << "; synchronization validation "
              << (validation ? "enabled" : "unavailable") << '\n';
  }
  destroyMessenger(*instance, messenger, nullptr);
  assert(validationErrors == 0);
}
