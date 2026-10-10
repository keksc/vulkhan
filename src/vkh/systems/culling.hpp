#pragma once

#include <array>
#include <cstddef>
#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

namespace vkh {

struct CullingPush {
  glm::vec4 frustumPlanes[6];
  uint32_t totalInstances;
  uint32_t totalCommands;
};
static_assert(offsetof(CullingPush, totalInstances) == 96);
static_assert(offsetof(CullingPush, totalCommands) == 100);
static_assert(sizeof(CullingPush) <= 128);

struct CullingDispatch {
  vk::Pipeline cull, finalize;
  vk::PipelineLayout layout;
  vk::DescriptorSet descriptors;
  vk::Buffer compacted, indirect, visibleCounts;
  uint32_t groupCount;
};

// Shared by entities and trees. Both pipelines have compatible layouts.
// Each view is captured in push constants and may reuse the same output
// buffers after the preceding view's draws have consumed them.
inline void recordCulling(vk::CommandBuffer cmd, const CullingPush &view,
                          const CullingDispatch &gpu) {
  const auto compute = vk::PipelineStageFlagBits::eComputeShader;
  const auto transfer = vk::PipelineStageFlagBits::eTransfer;
  const auto read = vk::AccessFlagBits::eShaderRead;
  const auto write = vk::AccessFlagBits::eShaderWrite;
  auto barrier = [](vk::Buffer buffer, vk::AccessFlags src,
                    vk::AccessFlags dst) {
    vk::BufferMemoryBarrier b{};
    b.srcAccessMask = src;
    b.dstAccessMask = dst;
    b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.buffer = buffer;
    b.size = VK_WHOLE_SIZE;
    return b;
  };
  // Include earlier compute writes even if a view issued no draw for a
  // batch. Its outputs still need protection against subsequent writes.
  const std::array before{barrier(gpu.visibleCounts, read | write,
                                  vk::AccessFlagBits::eTransferWrite),
                          barrier(gpu.indirect,
                                  vk::AccessFlagBits::eIndirectCommandRead |
                                      vk::AccessFlagBits::eHostWrite | write,
                                  write),
                          barrier(gpu.compacted, read | write, write)};
  cmd.pipelineBarrier(compute | vk::PipelineStageFlagBits::eDrawIndirect |
                          vk::PipelineStageFlagBits::eVertexShader |
                          vk::PipelineStageFlagBits::eHost,
                      compute | transfer, {}, nullptr, before, nullptr);

  if (gpu.groupCount) {
    cmd.fillBuffer(gpu.visibleCounts, 0,
                   vk::DeviceSize(gpu.groupCount) * sizeof(uint32_t), 0);
    const auto reset = barrier(
        gpu.visibleCounts, vk::AccessFlagBits::eTransferWrite, read | write);
    cmd.pipelineBarrier(transfer, compute, {}, nullptr, reset, nullptr);
  }
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, gpu.layout, 0,
                         gpu.descriptors, {});
  cmd.pushConstants(gpu.layout, vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(view), &view);
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, gpu.cull);
  if (view.totalInstances)
    cmd.dispatch((view.totalInstances + 63) / 64, 1, 1);

  const auto counts = barrier(gpu.visibleCounts, write, read);
  cmd.pipelineBarrier(compute, compute, {}, nullptr, counts, nullptr);
  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, gpu.finalize);
  if (view.totalCommands)
    cmd.dispatch((view.totalCommands + 63) / 64, 1, 1);

  // Make both outputs visible: indirect reads consume the command counts,
  // while vertex shaders consume the newly compacted instance array.
  const std::array ready{
      barrier(gpu.indirect, write, vk::AccessFlagBits::eIndirectCommandRead),
      barrier(gpu.compacted, write, read)};
  cmd.pipelineBarrier(compute,
                      vk::PipelineStageFlagBits::eDrawIndirect |
                          vk::PipelineStageFlagBits::eVertexShader,
                      {}, nullptr, ready, nullptr);
}

} // namespace vkh
