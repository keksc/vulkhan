#include "furSys.hpp"

#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../renderer.hpp"
#include "../../swapChain.hpp"
#include "../terrain/terrainChunks.hpp"
#include "wolfPacks.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numeric>
#include <print>

namespace vkh {

FurSys::FurSys(EngineContext &context, vk::DescriptorSetLayout textures,
               vk::DescriptorSetLayout instances,
               std::span<const FurBundle> groom, vk::DescriptorImageInfo coat)
    : System(context), coat(coat), bundleCount(groom.size()) {
  if (groom.empty())
    throw std::runtime_error("Strand renderer needs a groom");
  bundles = std::make_unique<Buffer<FurBundle>>(
      context,
      vk::BufferUsageFlagBits::eStorageBuffer |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal, bundleCount);
  Buffer<FurBundle> staging(context, vk::BufferUsageFlagBits::eTransferSrc,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent,
                            bundleCount);
  staging.map();
  staging.write(groom.data());
  auto cmd = beginSingleTimeCommands(context);
  bundles->recordCopyFromBuffer(cmd, staging);
  vk::MemoryBarrier upload(vk::AccessFlagBits::eTransferWrite,
                           vk::AccessFlagBits::eShaderRead);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eComputeShader, {}, 1, &upload,
                      0, nullptr, 0, nullptr);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  std::vector<vk::DescriptorSetLayoutBinding> bindings;
  for (uint32_t i = 0; i < 5; ++i)
    bindings.emplace_back(i, vk::DescriptorType::eStorageBuffer, 1,
                          vk::ShaderStageFlagBits::eCompute |
                              ((i == 1 || i == 2 || i == 4)
                                   ? vk::ShaderStageFlagBits::eFragment
                                   : vk::ShaderStageFlags{}));
  bindings.emplace_back(5, vk::DescriptorType::eCombinedImageSampler, 1,
                        vk::ShaderStageFlagBits::eCompute);
  bindings.emplace_back(6, vk::DescriptorType::eCombinedImageSampler, 1,
                        vk::ShaderStageFlagBits::eCompute);
  layout = buildDescriptorSetLayout(context, bindings);
  vk::SamplerCreateInfo sampler{};
  sampler.magFilter = sampler.minFilter = vk::Filter::eNearest;
  sampler.mipmapMode = vk::SamplerMipmapMode::eNearest;
  sampler.addressModeU = sampler.addressModeV = sampler.addressModeW =
      vk::SamplerAddressMode::eClampToEdge;
  depthSampler = context.vulkan.device.createSampler(sampler);

  std::array layouts{context.vulkan.globalDescriptorSetLayout, textures,
                     instances, layout};
  vk::PushConstantRange range(vk::ShaderStageFlagBits::eCompute |
                                  vk::ShaderStageFlagBits::eFragment,
                              0, sizeof(Push));
  vk::PipelineLayoutCreateInfo info{};
  info.setLayoutCount = layouts.size();
  info.pSetLayouts = layouts.data();
  info.pushConstantRangeCount = 1;
  info.pPushConstantRanges = &range;
  lod = std::make_unique<ComputePipeline>(
      context, "shaders/wolf/furLod.comp.spv", info, "fur LOD");
  raster = std::make_unique<ComputePipeline>(
      context, "shaders/wolf/furRaster.comp.spv", info, "fur raster");
  shade = std::make_unique<ComputePipeline>(
      context, "shaders/wolf/furShade.comp.spv", info, "fur shading");
  PipelineCreateInfo graphics{};
  graphics.layoutInfo = info;
  graphics.vertpath = "shaders/wolf/furComposite.vert.spv";
  graphics.fragpath = "shaders/wolf/furComposite.frag.spv";
  graphics.depthStencilInfo.depthWriteEnable = false;
  GraphicsPipeline::enableAlphaBlending(graphics);
  composite = std::make_unique<GraphicsPipeline>(context, graphics,
                                                 "fur reconstruction");
  frames.resize(context.vulkan.maxFramesInFlight);
  for (auto &frame : frames) {
    frame.set = context.vulkan.globalDescriptorAllocator->allocate(layout);
    frame.dispatch = std::make_unique<Buffer<Dispatch>>(
        context,
        vk::BufferUsageFlagBits::eStorageBuffer |
            vk::BufferUsageFlagBits::eIndirectBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal,
        wildlife::WolfPacks::kWolfCount);
  }
  std::println(
      "[fur] experimental compute strands: {} bundles, "
      "up to {} strands/wolf; VULKHAN_WOLF_FUR=0 for bare skin",
      bundleCount,
      static_cast<uint32_t>(std::accumulate(
          groom.begin(), groom.end(), 0.f,
          [](float sum, const FurBundle &b) { return sum + b.parameters.x; })));
}

FurSys::~FurSys() {
  composite.reset();
  shade.reset();
  raster.reset();
  lod.reset();
  context.vulkan.device.destroySampler(depthSampler);
  context.vulkan.device.destroyDescriptorSetLayout(layout);
}

bool FurSys::prepare(std::span<const glm::mat4> models) {
  ready = false;
  visible.clear();
  instanceCount = models.size();
  auto extent = context.vulkan.swapChain->getSwapChainExtent();
  const uint64_t pixels = uint64_t(extent.width) * extent.height;
  const auto maxRange =
      context.vulkan.physicalDeviceProperties.limits.maxStorageBufferRange;
  if (pixels == 0 || pixels * sizeof(uint64_t) > maxRange ||
      pixels > std::numeric_limits<uint32_t>::max())
    return false; // Keep cards if the framebuffer exceeds storage limits.
  const auto vp = context.camera.projectionMatrix * context.camera.viewMatrix;
  const auto planes = terrain::extractFrustumPlanes(vp);
  glm::vec2 minimum(extent.width, extent.height), maximum(0);
  for (uint32_t i = 0; i < models.size(); ++i) {
    const auto &model = models[i];
    const glm::vec3 center = glm::vec3(model * glm::vec4(0, 1.1f, 0, 1));
    const float scale = std::max({glm::length(glm::vec3(model[0])),
                                  glm::length(glm::vec3(model[1])),
                                  glm::length(glm::vec3(model[2]))});
    const float radius = 3.5f * scale;
    bool culled = glm::distance(center, context.camera.position) > 500.f;
    for (const auto &plane : planes)
      culled |= glm::dot(glm::vec3(plane), center) + plane.w < -radius;
    if (culled)
      continue;
    visible.push_back(i);
    for (uint32_t corner = 0; corner < 8; ++corner) {
      const glm::vec3 offset((corner & 1) ? radius : -radius,
                             (corner & 2) ? radius : -radius,
                             (corner & 4) ? radius : -radius);
      const auto clip = vp * glm::vec4(center + offset, 1);
      if (clip.w <= .001f) {
        minimum = glm::vec2(0);
        maximum = glm::vec2(extent.width, extent.height);
        break; // Near-plane crossing: conservative full-screen rectangle.
      }
      const auto pixel = (glm::vec2(clip) / clip.w * .5f + .5f) *
                         glm::vec2(extent.width, extent.height);
      minimum = glm::min(minimum, pixel - 4.f);
      maximum = glm::max(maximum, pixel + 4.f);
    }
  }
  if (visible.empty())
    return true; // No dispatch or scratch allocation when all wolves are
                 // hidden.
  const auto lo =
      glm::ivec2(glm::clamp(glm::floor(minimum), glm::vec2(0),
                            glm::vec2(extent.width, extent.height)));
  const auto hi =
      glm::ivec2(glm::clamp(glm::ceil(maximum), glm::vec2(0),
                            glm::vec2(extent.width, extent.height)));
  if (hi.x <= lo.x || hi.y <= lo.y)
    return true;
  scissor = vk::Rect2D(vk::Offset2D(lo.x, lo.y),
                       vk::Extent2D(hi.x - lo.x, hi.y - lo.y));
  push.window = {scissor.extent.width, scissor.extent.height, lo.x, lo.y};
  push.inverseProjView = glm::inverse(vp);
  auto &frame = frames[context.frameInfo.frameIndex];
  if (frame.capacity < pixels) {
    // Only this fence-completed slot is replaced, never another in-flight slot.
    const auto usage = vk::BufferUsageFlagBits::eStorageBuffer |
                       vk::BufferUsageFlagBits::eTransferDst;
    frame.center = std::make_unique<Buffer<uint64_t>>(
        context, usage, vk::MemoryPropertyFlagBits::eDeviceLocal, pixels);
    frame.conservative = std::make_unique<Buffer<uint64_t>>(
        context, usage, vk::MemoryPropertyFlagBits::eDeviceLocal, pixels);
    frame.shades = std::make_unique<Buffer<uint32_t>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eDeviceLocal, pixels);
    frame.capacity = pixels;
  }
  ready = true;
  return true;
}

void FurSys::bind(vk::CommandBuffer cmd, Pipeline &pipeline,
                  vk::PipelineBindPoint point, vk::DescriptorSet textures,
                  vk::DescriptorSet instances) {
  pipeline.bind(cmd);
  std::array sets{
      context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      textures, instances, frames[context.frameInfo.frameIndex].set};
  cmd.bindDescriptorSets(point, pipeline.getLayout(), 0, sets.size(),
                         sets.data(), 0, nullptr);
  cmd.pushConstants(pipeline.getLayout(),
                    vk::ShaderStageFlagBits::eCompute |
                        vk::ShaderStageFlagBits::eFragment,
                    0, sizeof(Push), &push);
}

void FurSys::rasterize(vk::DescriptorSet textures, vk::DescriptorSet instances,
                       uint32_t poseCount, uint32_t jointCount) {
  if (!ready)
    return;
  auto cmd = context.frameInfo.cmd;
  auto &frame = frames[context.frameInfo.frameIndex];
  auto &swap = *context.vulkan.swapChain;
  const uint32_t image = renderer::getCurrentImageIndex();
  DescriptorWriter writer(context);
  writer.writeBuffer(0, bundles->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(1, frame.center->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(2, frame.conservative->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(3, frame.dispatch->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeBuffer(4, frame.shades->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.writeImage(5,
                    {depthSampler, swap.getResolvedDepthImageView(image),
                     vk::ImageLayout::eDepthStencilReadOnlyOptimal},
                    vk::DescriptorType::eCombinedImageSampler);
  writer.writeImage(6, coat, vk::DescriptorType::eCombinedImageSampler);
  writer.updateSet(frame.set);
  debug::beginLabel(context, cmd, "Strand fur: LOD/raster/shade",
                    {.6f, .4f, .2f, 1});

  vk::ImageMemoryBarrier depth{};
  depth.srcAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  depth.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  depth.oldLayout = vk::ImageLayout::eDepthStencilAttachmentOptimal;
  depth.newLayout = vk::ImageLayout::eDepthStencilReadOnlyOptimal;
  depth.srcQueueFamilyIndex = depth.dstQueueFamilyIndex =
      vk::QueueFamilyIgnored;
  depth.image = swap.getResolvedDepthImage(image);
  depth.subresourceRange = {vk::ImageAspectFlagBits::eDepth, 0, 1, 0, 1};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eLateFragmentTests,
                      vk::PipelineStageFlagBits::eComputeShader, {}, 0, nullptr,
                      0, nullptr, 1, &depth);
  // Order previous slot reads/writes before this frame's clears and dispatch.
  vk::MemoryBarrier reuse(
      vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite |
          vk::AccessFlagBits::eIndirectCommandRead,
      vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eShaderWrite);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader |
                          vk::PipelineStageFlagBits::eFragmentShader |
                          vk::PipelineStageFlagBits::eDrawIndirect,
                      vk::PipelineStageFlagBits::eTransfer |
                          vk::PipelineStageFlagBits::eComputeShader,
                      {}, 1, &reuse, 0, nullptr, 0, nullptr);
  const vk::DeviceSize bytes =
      uint64_t(push.window.x) * push.window.y * sizeof(uint64_t);
  cmd.fillBuffer(*frame.center, 0, bytes, ~0u);
  cmd.fillBuffer(*frame.conservative, 0, bytes, ~0u);
  vk::MemoryBarrier clear(vk::AccessFlagBits::eTransferWrite,
                          vk::AccessFlagBits::eShaderRead |
                              vk::AccessFlagBits::eShaderWrite);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eComputeShader, {}, 1, &clear,
                      0, nullptr, 0, nullptr);
  push.counts = {instanceCount, poseCount, jointCount, bundleCount};
  bind(cmd, *lod, vk::PipelineBindPoint::eCompute, textures, instances);
  cmd.dispatch((instanceCount + 31) / 32, 1, 1);
  vk::MemoryBarrier indirect(vk::AccessFlagBits::eShaderWrite,
                             vk::AccessFlagBits::eIndirectCommandRead |
                                 vk::AccessFlagBits::eShaderRead);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eDrawIndirect |
                          vk::PipelineStageFlagBits::eComputeShader,
                      {}, 1, &indirect, 0, nullptr, 0, nullptr);
  for (uint32_t i : visible) {
    push.counts.x = i;
    bind(cmd, *raster, vk::PipelineBindPoint::eCompute, textures, instances);
    cmd.dispatchIndirect(*frame.dispatch, i * sizeof(Dispatch));
  }
  vk::MemoryBarrier rasterDone(vk::AccessFlagBits::eShaderWrite,
                               vk::AccessFlagBits::eShaderRead);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader, {}, 1,
                      &rasterDone, 0, nullptr, 0, nullptr);
  bind(cmd, *shade, vk::PipelineBindPoint::eCompute, textures, instances);
  cmd.dispatch((push.window.x + 7) / 8, (push.window.y + 7) / 8, 1);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eFragmentShader, {}, 1,
                      &rasterDone, 0, nullptr, 0, nullptr);
  depth.srcAccessMask = vk::AccessFlagBits::eShaderRead;
  depth.dstAccessMask = vk::AccessFlagBits::eDepthStencilAttachmentRead |
                        vk::AccessFlagBits::eDepthStencilAttachmentWrite;
  std::swap(depth.oldLayout, depth.newLayout);
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eEarlyFragmentTests |
                          vk::PipelineStageFlagBits::eLateFragmentTests,
                      {}, 0, nullptr, 0, nullptr, 1, &depth);
  debug::endLabel(context, cmd);
}

void FurSys::render(vk::DescriptorSet textures, vk::DescriptorSet instances) {
  if (!ready)
    return;
  auto cmd = context.frameInfo.cmd;
  debug::beginLabel(context, cmd, "Strand fur: reconstruct/composite",
                    {.6f, .4f, .2f, 1});
  bind(cmd, *composite, vk::PipelineBindPoint::eGraphics, textures, instances);
  cmd.setScissor(0, 1, &scissor);
  cmd.draw(3, 1, 0, 0);
  const vk::Rect2D full({}, context.vulkan.swapChain->getSwapChainExtent());
  cmd.setScissor(0, 1, &full);
  debug::endLabel(context, cmd);
}

} // namespace vkh
