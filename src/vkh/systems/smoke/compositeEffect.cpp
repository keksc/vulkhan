#include "compositeEffect.hpp"

#include <glm/glm.hpp>

#include <string>

#include "../../debug.hpp"
#include "../../descriptors.hpp"

namespace vkh {

SmokeCompositeEffect::SmokeCompositeEffect(EngineContext &context,
                                           SmokeSys &smokeSys)
    : context{context}, smokeSys{smokeSys} {
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
  samplerInfo.unnormalizedCoordinates = false;
  if (context.vulkan.device.createSampler(
          &samplerInfo, nullptr, &smokeVolumeSampler) != vk::Result::eSuccess)
    throw std::runtime_error("failed to create smoke volume sampler!");

  createLightsBuffer();
  createTransmittancePass();

  // Two sample point lights near the origin, just so smoke lighting has
  // something to react to before real scene lights (glTF-loaded) exist.
  PointLight sampleLights[2]{};
  sampleLights[0].positionAndRadius = glm::vec4(3.0f, 4.0f, 2.0f, 0.0f);
  sampleLights[0].colorAndIntensity = glm::vec4(1.0f, 0.85f, 0.6f, 40.0f);
  sampleLights[1].positionAndRadius = glm::vec4(-4.0f, 3.0f, -2.0f, 0.0f);
  sampleLights[1].colorAndIntensity = glm::vec4(0.5f, 0.7f, 1.0f, 30.0f);
  setPointLights(sampleLights, 2);
}

SmokeCompositeEffect::~SmokeCompositeEffect() {
  context.vulkan.device.destroySampler(smokeVolumeSampler);
  context.vulkan.device.destroyPipelineLayout(transmittancePipelineLayout,
                                              nullptr);
  context.vulkan.device.destroyDescriptorSetLayout(transmittanceSetLayout,
                                                   nullptr);
}

void SmokeCompositeEffect::createLightsBuffer() {
  // Fixed capacity, allocated once - see MAX_LIGHT_CAPACITY in lights.hpp.
  // Host-visible/coherent and persistently mapped, since setPointLights()
  // can be called from outside the render loop and this buffer is only
  // ever read by the compute passes.
  lightsBuffer = std::make_unique<Buffer<PointLight>>(
      context, vk::BufferUsageFlagBits::eStorageBuffer,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      MAX_LIGHT_CAPACITY);
  lightsBuffer->map();
  // Contents past activeLightCount are left uninitialized - both shaders
  // only ever loop up to the count passed via push constant, never past
  // it, so unwritten slots are never read.
}

void SmokeCompositeEffect::createTransmittanceImages(uint32_t imageCount) {
  shadowCache.assign(imageCount, {});
  ImageCreateInfo_empty3D createInfo{};
  // eTransferDst is needed purely for the one-time clear-to-1.0 below
  // (vkCmdClearColorImage requires the target image have this usage bit).
  createInfo.usage = vk::ImageUsageFlagBits::eStorage |
                     vk::ImageUsageFlagBits::eSampled |
                     vk::ImageUsageFlagBits::eTransferDst;
  createInfo.layout = vk::ImageLayout::eGeneral;
  createInfo.size = smokeSys.cellCount / kTransmittanceStride;
  // Single-channel now (was rgba32f packing 4 lights/texel) - one volume
  // per light instead of one volume per 4 lights. Same total bytes for a
  // given light count; see the memory/perf note on
  // transmittanceVolumes in the header.
  createInfo.format = vk::Format::eR32Sfloat;

  transmittanceVolumes.clear();
  transmittanceVolumes.resize(imageCount);

  vk::CommandBuffer cmd = beginSingleTimeCommands(context);
  vk::ClearColorValue clearValue{};
  clearValue.setFloat32({1.0f, 1.0f, 1.0f, 1.0f});
  vk::ImageSubresourceRange range{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};

  for (uint32_t i = 0; i < imageCount; i++) {
    transmittanceVolumes[i].reserve(TRANSMITTANCE_CAPACITY);
    for (int l = 0; l < TRANSMITTANCE_CAPACITY; l++) {
      std::string str =
          std::format("smoke transmittance volume [img {}, light {}]", i, l);
      createInfo.name = str.c_str();
      transmittanceVolumes[i].emplace_back(context, createInfo);

      // Initialize to fully transmissive (1.0) rather than leaving
      // undefined: even though PARTIALLY_BOUND_BIT means an unwritten
      // slot's *descriptor* is never read past shadowedLightCount, every
      // slot up to a light's current shadowedLightCount DOES get sampled
      // every frame, and a light freshly promoted into range this frame
      // (before beforeDispatch() has run for it) should read as
      // "unshadowed" rather than garbage.
      cmd.clearColorImage(transmittanceVolumes[i][l], vk::ImageLayout::eGeneral,
                          clearValue, range);
    }
  }
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void SmokeCompositeEffect::writeTransmittanceDescriptorSet(
    uint32_t imageIndex) {
  DescriptorWriter writer(context);
  // Sampled (not storage) - the march below uses hardware trilinear with
  // the dye's REPEAT sampler, see transmittance.comp.
  writer.writeImage(0, smokeSys.dyeImage->getDescriptorInfo(smokeVolumeSampler),
                    vk::DescriptorType::eCombinedImageSampler);

  // Binding 1: descriptor-indexed array of per-light transmittance
  // volumes. Every slot's image exists from construction (see
  // createTransmittanceImages()), so all TRANSMITTANCE_CAPACITY
  // descriptors are written here, once, regardless of the CURRENT active
  // light count - which slots are semantically "in use" is purely a
  // per-frame push-constant value (shadowedLightCount), never a
  // descriptor-set property. That's what makes light count changes at
  // runtime free: setPointLights() never needs to touch this descriptor
  // set. PARTIALLY_BOUND_BIT is kept on the layout as a defensive
  // allowance (e.g. if TRANSMITTANCE_CAPACITY is ever raised without
  // immediately re-allocating every slot's image), but isn't load-bearing
  // for the common case below since every slot IS written.
  std::vector<vk::DescriptorImageInfo> transInfos;
  transInfos.reserve(TRANSMITTANCE_CAPACITY);
  for (int l = 0; l < TRANSMITTANCE_CAPACITY; l++)
    transInfos.push_back(transmittanceVolumes[imageIndex][l].getDescriptorInfo(
        context.vulkan.defaultSampler));
  writer.writeImageArray(1, transInfos, vk::DescriptorType::eStorageImage);

  writer.writeBuffer(2, lightsBuffer->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);
  writer.updateSet(transmittanceSet[imageIndex]);
}

void SmokeCompositeEffect::createTransmittancePass() {
  std::vector<vk::DescriptorSetLayoutBinding> bindings = {
      // Binding 0: dye volume (read) - combined image sampler (REPEAT,
      // trilinear): transmittance.comp marches it with one hardware fetch
      // per step.
      {0, vk::DescriptorType::eCombinedImageSampler, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      // Binding 1: per-light transmittance volumes (write), descriptor-
      // indexed array sized TRANSMITTANCE_CAPACITY. Must be the LAST
      // binding in the set (Vulkan requires the variable/partially-bound
      // binding to be last).
      {1, vk::DescriptorType::eStorageImage, TRANSMITTANCE_CAPACITY,
       vk::ShaderStageFlagBits::eCompute, nullptr},
      // Binding 2: point lights SSBO - same lightsBuffer as binding 4 of
      // the composite pass's own set, just re-bound here since this is a
      // separate pipeline/set.
      {2, vk::DescriptorType::eStorageBuffer, 1,
       vk::ShaderStageFlagBits::eCompute, nullptr},
  };

  // Binding 1 gets PARTIALLY_BOUND_BIT: not every one of its
  // TRANSMITTANCE_CAPACITY slots needs a valid descriptor, only the first
  // shadowedLightCount (see writeTransmittanceDescriptorSet()). Requires
  // VkPhysicalDeviceDescriptorIndexingFeatures::descriptorBindingPartiallyBound
  // enabled on the device - see the note in chat about main.cpp/device
  // setup.
  std::vector<vk::DescriptorBindingFlags> bindingFlags = {
      {},                                             // binding 0: no flags
      vk::DescriptorBindingFlagBits::ePartiallyBound, // binding 1
      {},                                             // binding 2: no flags
  };

  vk::DescriptorSetLayoutBindingFlagsCreateInfo flagsInfo{};
  flagsInfo.bindingCount = static_cast<uint32_t>(bindingFlags.size());
  flagsInfo.pBindingFlags = bindingFlags.data();

  // buildDescriptorSetLayout (descriptors.hpp) already forwards
  // flags/pNext into vk::DescriptorSetLayoutCreateInfo and throws on
  // failure via the vk::Device throwing overload - no need to build the
  // create-info struct or check the result by hand here.
  transmittanceSetLayout = buildDescriptorSetLayout(
      context, bindings, vk::DescriptorSetLayoutCreateFlags{}, &flagsInfo);

  uint32_t imageCount =
      static_cast<uint32_t>(context.vulkan.swapChain->imageCount());
  createTransmittanceImages(imageCount);

  transmittanceSet.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; i++) {
    transmittanceSet[i] = context.vulkan.globalDescriptorAllocator->allocate(
        transmittanceSetLayout);
    writeTransmittanceDescriptorSet(i);
  }

  vk::PushConstantRange pcRange{};
  pcRange.stageFlags = vk::ShaderStageFlagBits::eCompute;
  pcRange.offset = 0;
  pcRange.size = sizeof(TransmittancePush);

  vk::PipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.setLayoutCount = 1;
  pipelineLayoutInfo.pSetLayouts = &transmittanceSetLayout;
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pcRange;

  if (context.vulkan.device.createPipelineLayout(
          &pipelineLayoutInfo, nullptr, &transmittancePipelineLayout) !=
      vk::Result::eSuccess)
    throw std::runtime_error("failed to create transmittance pipeline layout!");

  transmittancePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/smoke/transmittance.comp.spv",
      transmittancePipelineLayout, "smoke transmittance");
}

void SmokeCompositeEffect::onImageCountChanged(uint32_t imageCount) {
  createTransmittanceImages(imageCount);
  transmittanceSet.resize(imageCount);
  for (uint32_t i = 0; i < imageCount; i++) {
    transmittanceSet[i] = context.vulkan.globalDescriptorAllocator->allocate(
        transmittanceSetLayout);
    writeTransmittanceDescriptorSet(i);
  }
}

float SmokeCompositeEffect::maxLightDistance() const {
  constexpr float kMaxLightDistanceFactor = 2.5f;
  glm::vec3 gridExtent = glm::vec3(smokeSys.cellCount) * smokeSys.cellSize;
  return glm::length(gridExtent) * kMaxLightDistanceFactor;
}

void SmokeCompositeEffect::beforeDispatch(vk::CommandBuffer cmd,
                                          uint32_t imageIndex) {
  if (!smokeSys.hasSmoke()) return;
  auto &stamp = shadowCache[imageIndex];
  if (stamp.step == smokeSys.stepCount() && stamp.lights == lightsVersion &&
      stamp.origin == smokeSys.gridOrigin) return;
  glm::vec3 boxMin = glm::vec3(smokeSys.gridOrigin) * smokeSys.cellSize;
  glm::vec3 boxMax = boxMin + glm::vec3(smokeSys.cellCount) * smokeSys.cellSize;
  float maxDist = maxLightDistance();

  int shadowedCount = glm::min(activeLightCount, TRANSMITTANCE_CAPACITY);

  bool anyLightInRange = false;
  for (int i = 0; i < shadowedCount; i++) {
    glm::vec3 lightPos = glm::vec3(lightsCpuCopy[i].positionAndRadius);
    glm::vec3 closest = glm::clamp(lightPos, boxMin, boxMax);
    if (glm::length(lightPos - closest) <= maxDist) {
      anyLightInRange = true;
      break;
    }
  }

  // No transmittance work at all this frame: every shadowed-capacity
  // light is far enough that its attenuation would be imperceptible (see
  // maxLightDistance()). The volumes simply keep whatever they last held
  // - safe, since they were initialized to 1.0 at creation and only ever
  // hold values computed under this same "close enough to matter"
  // assumption.
  if (shadowedCount == 0 || !anyLightInRange)
    return;
  stamp = {smokeSys.stepCount(), lightsVersion, smokeSys.gridOrigin};

  debug::beginLabel(context, cmd, "smoke: transmittance",
                    {0.9f, 0.7f, 0.2f, 1.0f});

  cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *transmittancePipeline);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         transmittancePipelineLayout, 0,
                         transmittanceSet[imageIndex], {});

  TransmittancePush pc{};
  pc.cellCount = glm::ivec4(smokeSys.cellCount, 0);
  pc.gridOrigin = glm::ivec4(smokeSys.gridOrigin, 0);
  pc.cellSize = smokeSys.cellSize;
  pc.maxLightDistance = maxDist;
  pc.shadowedLightCount = shadowedCount;
  pc.stride = kTransmittanceStride;
  cmd.pushConstants(transmittancePipelineLayout,
                    vk::ShaderStageFlagBits::eCompute, 0,
                    sizeof(TransmittancePush), &pc);

  // Matches transmittance.comp's local_size_{x,y,z} = 8. The volumes (and
  // therefore the dispatch) are 1/kTransmittanceStride of the dye grid.
  glm::ivec3 outCells = smokeSys.cellCount / kTransmittanceStride;
  glm::ivec3 groups = (outCells + glm::ivec3(7)) / 8;
  cmd.dispatch(groups.x, groups.y, groups.z);

  // Barrier covers every slot up to shadowedCount - the ones
  // transmittance.comp actually wrote this dispatch. Slots beyond
  // shadowedCount are never touched by the write or the read, so no
  // barrier is needed for them.
  std::vector<vk::ImageMemoryBarrier> barriers(shadowedCount);
  for (int l = 0; l < shadowedCount; l++) {
    auto &b = barriers[l];
    b.oldLayout = vk::ImageLayout::eGeneral;
    b.newLayout = vk::ImageLayout::eGeneral;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.subresourceRange.aspectMask = vk::ImageAspectFlagBits::eColor;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount = 1;
    b.srcAccessMask = vk::AccessFlagBits::eShaderWrite;
    b.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    b.image = transmittanceVolumes[imageIndex][l];
  }

  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eComputeShader,
                      vk::DependencyFlags{}, 0, nullptr, 0, nullptr,
                      static_cast<uint32_t>(barriers.size()), barriers.data());

  debug::endLabel(context, cmd);
}

void SmokeCompositeEffect::setPointLights(const PointLight *lights, int count) {
  ++lightsVersion;
  count = glm::clamp(count, 0, MAX_LIGHT_CAPACITY);
  lightsCpuCopy.assign(lights, lights + count);
  activeLightCount = count;

  if (count > 0) {
    // Only the active prefix is written/flushed - no need to touch the
    // full MAX_LIGHT_CAPACITY-sized buffer for a small light count.
    //
    // Buffer<T>::write()'s size parameter is in BYTES, not element count -
    // passing `count` directly here used to write only the first `count`
    // BYTES of lightsCpuCopy (e.g. 2 bytes for 2 lights) instead of
    // `count` PointLight structs (64 bytes for 2 lights), leaving nearly
    // all of both lights' actual data untouched and reading back whatever
    // was already in that host-visible memory - see the transmittance
    // debugging session in chat for how this surfaced (both lights'
    // slots showing identical, non-light-shaped stale data in RenderDoc).
    lightsBuffer->write(lightsCpuCopy.data(),
                        static_cast<vk::DeviceSize>(count) *
                            sizeof(PointLight));
    lightsBuffer->flush();
  }

  // No descriptor-set work needed here at all: every transmittance-array
  // slot's descriptor was written once, up front, for every swapchain
  // image (see writeTransmittanceDescriptorSet()/writeAdditionalDescriptors()
  // - both write all TRANSMITTANCE_CAPACITY slots unconditionally, since
  // each slot maps to a fixed Image3D that exists regardless of how many
  // lights are currently active). activeLightCount changing only changes
  // what beforeDispatch()/makePushConstants() put in the next frame's
  // push constants - it never invalidates a descriptor set, so light
  // count can change every frame with zero descriptor churn.
}

std::vector<vk::DescriptorSetLayoutBinding>
SmokeCompositeEffect::additionalBindings() const {
  return {
      // Binding 3: Smoke dye volume (Input) — sampler3D, periodic
      vk::DescriptorSetLayoutBinding{
          3, vk::DescriptorType::eCombinedImageSampler, 1,
          vk::ShaderStageFlagBits::eCompute, nullptr},
      // Binding 4: Point lights SSBO (Input) - runtime array, updated
      // from the CPU via setPointLights(). Not per-swapchain-image,
      // same as before.
      vk::DescriptorSetLayoutBinding{4, vk::DescriptorType::eStorageBuffer, 1,
                                     vk::ShaderStageFlagBits::eCompute,
                                     nullptr},
      // Binding 5: descriptor-indexed array of precomputed per-light
      // transmittance volumes (Input), sized TRANSMITTANCE_CAPACITY.
      // Must stay the LAST binding in this set - see
      // additionalBindingFlags().
      vk::DescriptorSetLayoutBinding{
          5, vk::DescriptorType::eCombinedImageSampler, TRANSMITTANCE_CAPACITY,
          vk::ShaderStageFlagBits::eCompute, nullptr}};
}

std::vector<vk::DescriptorBindingFlags>
SmokeCompositeEffect::additionalBindingFlags() const {
  // Parallel to additionalBindings() above: bindings 3-4 get no special
  // flags, binding 5 (the transmittance array) is PARTIALLY_BOUND_BIT so
  // only shadowedLightCount of its TRANSMITTANCE_CAPACITY slots need a
  // valid written descriptor.
  return {{}, {}, vk::DescriptorBindingFlagBits::ePartiallyBound};
}

void SmokeCompositeEffect::writeAdditionalDescriptors(DescriptorWriter &writer,
                                                      uint32_t imageIndex) {
  vk::DescriptorImageInfo smokeVolumeInfo =
      smokeSys.dyeImage->getDescriptorInfo(smokeVolumeSampler);
  writer.writeImage(3, smokeVolumeInfo,
                    vk::DescriptorType::eCombinedImageSampler);

  writer.writeBuffer(4, lightsBuffer->descriptorInfo(),
                     vk::DescriptorType::eStorageBuffer);

  // All TRANSMITTANCE_CAPACITY slots, unconditionally - see the comment
  // in writeTransmittanceDescriptorSet() for why this never needs
  // rewriting when the active light count changes. Requires
  // DescriptorWriter::writeImageArray(binding, span, type) - flagged in
  // chat as a needed addition to descriptors.hpp if it doesn't exist yet
  // (same shape as writeImage, just descriptorCount = span.size()).
  std::vector<vk::DescriptorImageInfo> transInfos;
  transInfos.reserve(TRANSMITTANCE_CAPACITY);
  for (int l = 0; l < TRANSMITTANCE_CAPACITY; l++)
    transInfos.push_back(transmittanceVolumes[imageIndex][l].getDescriptorInfo(
        smokeVolumeSampler));
  writer.writeImageArray(5, transInfos,
                         vk::DescriptorType::eCombinedImageSampler);
}

SmokeCompositeEffect::Push SmokeCompositeEffect::makePushConstants() const {
  Push push{};
  push.boxMin = glm::vec4(glm::vec3(smokeSys.gridOrigin) * smokeSys.cellSize,
                          smokeSys.cellSize);
  push.cellCount = glm::ivec4(smokeSys.cellCount, smokeSys.hasSmoke() ? 1 : 0);
  push.maxLightDistance = maxLightDistance();
  push.lightCount = activeLightCount;
  push.shadowedLightCount = glm::min(activeLightCount, TRANSMITTANCE_CAPACITY);
  // Same matrix the shader used to build from ubo.projView every pixel;
  // main.cpp fills ubo.projView = proj * view from these exact fields.
  push.invProjView =
      glm::inverse(context.camera.projectionMatrix * context.camera.viewMatrix);
  return push;
}

} // namespace vkh
