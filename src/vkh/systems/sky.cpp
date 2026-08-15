#include "sky.hpp"

#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../pipeline.hpp"
#include "../sceneBuilder.hpp"
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

void SkySys::createBakedDescriptorSetLayout() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      // Binding 0: milky way cubemap
      vk::DescriptorSetLayoutBinding{
          0, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr},
      // Binding 1: accretion disc turbulence texture
      vk::DescriptorSetLayoutBinding{
          1, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eFragment, nullptr}};

  bakedSetLayout = buildDescriptorSetLayout(context, bindings);
  debug::setObjName(context, vk::ObjectType::eDescriptorSetLayout,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkDescriptorSetLayout>(bakedSetLayout)),
                    "sky baked resources set layout");
}

// One-time compute bake of the milky way cubemap and accretion-disc
// turbulence texture. Both are static (no runtime dependency on time,
// camera, etc.), so this only ever runs once at startup - the black hole
// shader's actual gravitational lensing stays real-time in sky.frag, only
// its two "texture channels" (originally iChannel0/iChannel1 in the
// Shadertoy source) are baked ahead of time instead of computed per pixel
// per frame.
void SkySys::bakeEnvironment() {
  // ---- create the target images (storage-writable, then sampled) ----
  ImageCreateInfo_cubemapStorage cubemapInfo{};
  cubemapInfo.faceSize = MILKY_WAY_FACE_SIZE;
  cubemapInfo.format = vk::Format::eR16G16B16A16Sfloat;
  cubemapInfo.usage =
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
  cubemapInfo.layout = vk::ImageLayout::eGeneral;
  cubemapInfo.name = "milky way cubemap";
  milkyWayCubemap = std::make_unique<Image>(context, cubemapInfo);

  ImageCreateInfo_empty discInfo{};
  discInfo.size = {DISC_TURBULENCE_SIZE, DISC_TURBULENCE_SIZE};
  discInfo.format = vk::Format::eR8Unorm;
  discInfo.usage =
      vk::ImageUsageFlagBits::eStorage | vk::ImageUsageFlagBits::eSampled;
  discInfo.layout = vk::ImageLayout::eGeneral;
  discInfo.name = "disc turbulence texture";
  discTurbulence = std::make_unique<Image>(context, discInfo);

  // ---- one-off compute pipelines + descriptor sets for the bake itself
  // (destroyed at the end of this function - nothing here is needed once
  // baking is done) ----
  std::vector<vk::DescriptorSetLayoutBinding> storageBinding = {
      vk::DescriptorSetLayoutBinding{0, vk::DescriptorType::eStorageImage, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr}};
  vk::DescriptorSetLayout bakeSetLayout =
      buildDescriptorSetLayout(context, storageBinding);

  vk::DescriptorSet milkyWayBakeSet =
      context.vulkan.globalDescriptorAllocator->allocate(bakeSetLayout);
  vk::DescriptorSet discBakeSet =
      context.vulkan.globalDescriptorAllocator->allocate(bakeSetLayout);

  {
    vk::DescriptorImageInfo imageInfo{nullptr, milkyWayCubemap->getArrayView(),
                                      vk::ImageLayout::eGeneral};
    DescriptorWriter writer(context);
    writer.writeImage(0, imageInfo, vk::DescriptorType::eStorageImage);
    writer.updateSet(milkyWayBakeSet);
  }
  {
    vk::DescriptorImageInfo imageInfo{nullptr, discTurbulence->getView(),
                                      vk::ImageLayout::eGeneral};
    DescriptorWriter writer(context);
    writer.writeImage(0, imageInfo, vk::DescriptorType::eStorageImage);
    writer.updateSet(discBakeSet);
  }

  struct MilkyWayPush {
    uint32_t faceSize;
  };
  struct DiscPush {
    uint32_t texSize;
  };

  vk::PushConstantRange milkyWayPushRange{vk::ShaderStageFlagBits::eCompute, 0,
                                          sizeof(MilkyWayPush)};
  vk::PushConstantRange discPushRange{vk::ShaderStageFlagBits::eCompute, 0,
                                      sizeof(DiscPush)};

  vk::PipelineLayoutCreateInfo milkyWayLayoutInfo{};
  milkyWayLayoutInfo.setLayoutCount = 1;
  milkyWayLayoutInfo.pSetLayouts = &bakeSetLayout;
  milkyWayLayoutInfo.pushConstantRangeCount = 1;
  milkyWayLayoutInfo.pPushConstantRanges = &milkyWayPushRange;

  vk::PipelineLayoutCreateInfo discLayoutInfo{};
  discLayoutInfo.setLayoutCount = 1;
  discLayoutInfo.pSetLayouts = &bakeSetLayout;
  discLayoutInfo.pushConstantRangeCount = 1;
  discLayoutInfo.pPushConstantRanges = &discPushRange;

  ComputePipeline milkyWayPipeline(context, "shaders/sky/bakeMilkyWay.comp.spv",
                                   milkyWayLayoutInfo,
                                   "bake milky way cubemap");
  ComputePipeline discPipeline(context,
                               "shaders/sky/bakeDiscTurbulence.comp.spv",
                               discLayoutInfo, "bake disc turbulence");

  // ---- record + dispatch both bakes, then transition both images to
  // their final shader-read-only layout ----
  auto cmd = beginSingleTimeCommands(context);

  milkyWayPipeline.bind(cmd);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         milkyWayPipeline.getLayout(), 0, 1, &milkyWayBakeSet,
                         0, nullptr);
  MilkyWayPush milkyWayPush{MILKY_WAY_FACE_SIZE};
  cmd.pushConstants(milkyWayPipeline.getLayout(),
                    vk::ShaderStageFlagBits::eCompute, 0, sizeof(MilkyWayPush),
                    &milkyWayPush);
  uint32_t milkyWayGroups = (MILKY_WAY_FACE_SIZE + 7) / 8;
  cmd.dispatch(milkyWayGroups, milkyWayGroups, 6);

  discPipeline.bind(cmd);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         discPipeline.getLayout(), 0, 1, &discBakeSet, 0,
                         nullptr);
  DiscPush discPush{DISC_TURBULENCE_SIZE};
  cmd.pushConstants(discPipeline.getLayout(), vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(DiscPush), &discPush);
  uint32_t discGroups = (DISC_TURBULENCE_SIZE + 7) / 8;
  cmd.dispatch(discGroups, discGroups, 1);

  // Barrier so the layout transitions below wait for both bakes to finish
  // writing before anything reads the images.
  vk::MemoryBarrier barrier{vk::AccessFlagBits::eShaderWrite,
                            vk::AccessFlagBits::eShaderRead};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader |
                          vk::PipelineStageFlagBits::eFragmentShader,
                      vk::DependencyFlags{}, 1, &barrier, 0, nullptr, 0,
                      nullptr);

  milkyWayCubemap->recordTransitionLayout(
      cmd, vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6});
  discTurbulence->recordTransitionLayout(
      cmd, vk::ImageLayout::eShaderReadOnlyOptimal);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  context.vulkan.device.destroyDescriptorSetLayout(bakeSetLayout, nullptr);
}

SkySys::SkySys(EngineContext &context) : System(context) {
  bakeEnvironment();
  createBakedDescriptorSetLayout();

  bakedSet = context.vulkan.globalDescriptorAllocator->allocate(bakedSetLayout);
  {
    vk::DescriptorImageInfo milkyWayInfo =
        milkyWayCubemap->getDescriptorInfo(context.vulkan.defaultSampler);
    vk::DescriptorImageInfo discInfo =
        discTurbulence->getDescriptorInfo(context.vulkan.defaultSampler);

    DescriptorWriter writer(context);
    writer.writeImage(0, milkyWayInfo,
                      vk::DescriptorType::eCombinedImageSampler);
    writer.writeImage(1, discInfo, vk::DescriptorType::eCombinedImageSampler);
    writer.updateSet(bakedSet);
  }

  std::vector<vk::DescriptorSetLayout> setLayouts{
      context.vulkan.globalDescriptorSetLayout,
      bakedSetLayout,
  };

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = static_cast<uint32_t>(setLayouts.size());
  pipelineLayoutInfo.pSetLayouts = setLayouts.data();

  PipelineCreateInfo pipelineInfo{};
  pipelineInfo.layoutInfo = pipelineLayoutInfo;
  pipelineInfo.depthStencilInfo.depthTestEnable = true;
  pipelineInfo.depthStencilInfo.depthCompareOp = vk::CompareOp::eGreaterOrEqual;
  pipelineInfo.depthStencilInfo.depthWriteEnable = false;
  pipelineInfo.vertpath = "shaders/sky/sky.vert.spv";
  pipelineInfo.fragpath = "shaders/sky/sky.frag.spv";

  pipelineInfo.multisampleInfo.rasterizationSamples =
      context.vulkan.msaaSamples;

  pipeline = std::make_unique<GraphicsPipeline>(context, pipelineInfo, "sky");
}

SkySys::~SkySys() {
  if (context.vulkan.device && bakedSetLayout) {
    context.vulkan.device.destroyDescriptorSetLayout(bakedSetLayout, nullptr);
  }
}

void SkySys::render() {
  auto &cmd = context.frameInfo.cmd;

  debug::beginLabel(context, cmd, "sky rendering", {.3f, .3f, 1.f, 1.f});
  pipeline->bind(cmd);

  cmd.bindDescriptorSets(
      vk::PipelineBindPoint::eGraphics, pipeline->getLayout(), 0, 1,
      &context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex], 0,
      nullptr);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         pipeline->getLayout(), 1, 1, &bakedSet, 0, nullptr);

  cmd.draw(3, 1, 0, 0);
  debug::endLabel(context, cmd);
}

} // namespace vkh
