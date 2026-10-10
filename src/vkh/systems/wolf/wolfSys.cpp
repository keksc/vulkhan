#include "wolfSys.hpp"
#include "furSys.hpp"

#include "../../camera.hpp"
#include "../../debug.hpp"
#include "../../descriptors.hpp"
#include "../../sceneBuilder.hpp"
#include "../terrain/terrainChunks.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <limits>
#include <print>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace vkh {

WolfSys::WolfSys(EngineContext &context, vk::DescriptorSetLayout texturesLayout)
    : System(context) {
  scene =
      std::make_shared<Scene<EntitySys::Vertex>>(context, "models/wolf.glb");
  const char *furMode = std::getenv("VULKHAN_WOLF_FUR");
  const bool wantStrands = !furMode || furMode[0] != '0';
  const bool canStrand = wantStrands && context.vulkan.strandFurAvailable;
  if (scene->lodMeshGroups.size() != 3 || scene->animations.empty() ||
      scene->gltf.images.size() != 1)
    throw std::runtime_error(
        "Wolf needs three skin LODs, a run clip and atlas");
  for (size_t i = 0; i < 3; ++i) {
    if (scene->lodMeshGroups[i].size() != 1)
      throw std::runtime_error("Wolf LOD must contain exactly one mesh");
    lodMeshes[i] = scene->lodMeshGroups[i][0];
    const auto &mesh = scene->meshes[lodMeshes[i]];
    if (!mesh.skinIndex || mesh.primitives.size() != 1 ||
        mesh.primitives[0].furParameters.empty())
      throw std::runtime_error(
          "Wolf needs hairless skin with surface fur attributes");
  }
  std::vector<FurBundle> groom;
  if (canStrand) {
    const auto &primitive = scene->meshes[lodMeshes[0]].primitives[0];
    groom = buildSurfaceFurGroom<EntitySys::Vertex>(
        scene->vertices,
        std::span(scene->indices)
            .subspan(primitive.indexOffset, primitive.indexCount),
        primitive.furParameters, primitive.furDirections);
    if (groom.empty())
      throw std::runtime_error("Wolf has no enabled fur-emitting surface");
  }
  scene->uploadModelGPU(texturesLayout);
  const char *debug = std::getenv("VULKHAN_WOLF_DEBUG");
  debugWolves = debug && debug[0] == '1';
  bakePoses();
  instanceLayout = buildDescriptorSetLayout(
      context,
      {{0, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute |
            vk::ShaderStageFlagBits::eFragment},
       {1, vk::DescriptorType::eStorageBuffer, 1,
        vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eCompute}});
  createPipelines(texturesLayout);
  for (uint32_t frame = 0; frame < context.vulkan.maxFramesInFlight; ++frame) {
    auto buffer = std::make_unique<Buffer<Instance>>(
        context, vk::BufferUsageFlagBits::eStorageBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible |
            vk::MemoryPropertyFlagBits::eHostCoherent,
        wildlife::WolfPacks::kWolfCount);
    buffer->map();
    auto set =
        context.vulkan.globalDescriptorAllocator->allocate(instanceLayout);
    DescriptorWriter writer(context);
    writer.writeBuffer(0, buffer->descriptorInfo(),
                       vk::DescriptorType::eStorageBuffer);
    writer.writeBuffer(1, poses->descriptorInfo(),
                       vk::DescriptorType::eStorageBuffer);
    writer.updateSet(set);
    instances.push_back(std::move(buffer));
    instanceSets.push_back(set);
  }
  if (canStrand) {
    strandFur = std::make_unique<FurSys>(
        context, texturesLayout, instanceLayout, groom,
        scene->images[0].getDescriptorInfo(context.vulkan.defaultSampler));
  } else if (wantStrands) {
    std::println(
        "[fur] 64-bit shader atomics unavailable; drawing the hairless skin");
  }
}

WolfSys::~WolfSys() {
  strandFur.reset();
  basePipeline.reset();
  if (instanceLayout)
    context.vulkan.device.destroyDescriptorSetLayout(instanceLayout);
}

void WolfSys::bakePoses() {
  const auto &skin = scene->skins[*scene->meshes[lodMeshes[0]].skinIndex];
  jointCount = static_cast<uint32_t>(skin.joints.size());
  const auto &clip = scene->animations[0];
  const float duration = clip.end - clip.start;
  if (jointCount == 0 || duration <= 0 ||
      skin.inverseBindMatrices.size() != jointCount)
    throw std::runtime_error("Invalid wolf run animation/skin");
  std::vector<glm::mat4> table;
  table.reserve(kPoseCount * jointCount);
  // Cache one whole loop. GPU vertex skinning interpolates neighbouring
  // frames, shared by every wolf; there is no per-wolf CPU skeletal solve.
  for (uint32_t frame = 0; frame < kPoseCount; ++frame) {
    // The authored clip's planted paws advance toward the +Z muzzle.
    // Sample in reverse so stance travels toward the tail as the body
    // moves forward; steering phase still advances with travelled distance.
    scene->updateAnimation(0, clip.end - duration * float(frame) / kPoseCount);
    for (uint32_t j = 0; j < jointCount; ++j)
      table.push_back(scene->nodes[skin.joints[j]].globalTransform *
                      skin.inverseBindMatrices[j]);
  }
  // This immutable table is read by the body and hair cards of every wolf. Keep
  // it in device-local memory rather than repeatedly fetching host RAM over
  // PCIe.
  poses = std::make_unique<Buffer<glm::mat4>>(
      context,
      vk::BufferUsageFlagBits::eStorageBuffer |
          vk::BufferUsageFlagBits::eTransferDst,
      vk::MemoryPropertyFlagBits::eDeviceLocal,
      static_cast<uint32_t>(table.size()));
  Buffer<glm::mat4> staging(context, vk::BufferUsageFlagBits::eTransferSrc,
                            vk::MemoryPropertyFlagBits::eHostVisible |
                                vk::MemoryPropertyFlagBits::eHostCoherent,
                            static_cast<uint32_t>(table.size()));
  staging.map();
  staging.write(table.data());
  staging.unmap();
  auto cmd = beginSingleTimeCommands(context);
  vk::BufferCopy copy{0, 0, poses->getSize()};
  cmd.copyBuffer(staging, *poses, 1, &copy);
  vk::BufferMemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.srcQueueFamilyIndex = vk::QueueFamilyIgnored;
  barrier.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  barrier.buffer = *poses;
  barrier.size = vk::WholeSize;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eVertexShader |
                          vk::PipelineStageFlagBits::eComputeShader,
                      {}, 0, nullptr, 1, &barrier, 0, nullptr);
  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);
}

void WolfSys::createPipelines(vk::DescriptorSetLayout texturesLayout) {
  std::array layouts{context.vulkan.globalDescriptorSetLayout, texturesLayout,
                     instanceLayout};
  vk::PushConstantRange range{};
  range.stageFlags =
      vk::ShaderStageFlagBits::eVertex | vk::ShaderStageFlagBits::eFragment;
  range.size = sizeof(Push);
  PipelineCreateInfo info{};
  info.layoutInfo.setLayoutCount = layouts.size();
  info.layoutInfo.pSetLayouts = layouts.data();
  info.layoutInfo.pushConstantRangeCount = 1;
  info.layoutInfo.pPushConstantRanges = &range;
  info.bindingDescriptions = EntitySys::Vertex::getBindingDescriptions();
  info.attributeDescriptions = EntitySys::Vertex::getAttributeDescriptions();
  info.vertpath = "shaders/wolf/wolf.vert.spv";
  info.fragpath = "shaders/wolf/wolf.frag.spv";
  // glTF's outward CCW triangles remain counterclockwise under this
  // engine's Y-flipped projection and positive-height viewport. Using
  // CLOCKWISE + back-face culling kept only the wolf's inside surfaces.
  info.rasterizationInfo.frontFace = vk::FrontFace::eCounterClockwise;
  info.rasterizationInfo.cullMode = vk::CullModeFlagBits::eBack;
  info.multisampleInfo.rasterizationSamples = context.vulkan.msaaSamples;
  basePipeline = std::make_unique<GraphicsPipeline>(context, info, "wolf skin");
}

void WolfSys::update(std::span<const wildlife::WolfPacks::Wolf> wolves) {
  if (wolves.size() != wildlife::WolfPacks::kWolfCount)
    throw std::invalid_argument(
        "Wolf renderer needs a complete wildlife snapshot");
  groups = {{{0, 0, 0}, {0, 0, 1}, {0, 0, 2}}};
  uint32_t next = 0;
  activeCount = 0;
  nearestDistance = std::numeric_limits<float>::max();
  for (size_t group = 0; group < groups.size(); ++group) {
    groups[group].first = next;
    for (size_t i = 0; i < wolves.size(); ++i) {
      const auto &wolf = wolves[i];
      if (!wolf.active)
        continue;
      const float d = glm::distance(context.camera.position, wolf.position);
      const size_t bucket = d < 45 ? 0 : d < 110 ? 1 : 2;
      if (bucket != group)
        continue;
      auto &instance = cpuInstances[next++];
      instance.model = wolf.modelMatrix();
      instance.animation = {glm::fract(wolf.phase) * kPoseCount,
                            .86f + float(i % 5) * .06f, wolf.motionBlend,
                            float(i) * .618034f};
      constexpr std::array coats{glm::vec4(.82f, .87f, .95f, 0.f),
                                 glm::vec4(.94f, .96f, 1.f, .90f),
                                 glm::vec4(.38f, .40f, .44f, 0.f)};
      instance.coat = studioMode ? glm::vec4(1, 1, 1, 0)
                                 : coats[i / wildlife::WolfPacks::kPackSize];
      if (studioMode)
        instance.animation = {0, 1, 0, 0};
      ++groups[group].count;
      ++activeCount;
      if (d < nearestDistance) {
        nearestDistance = d;
        nearestPosition = wolf.position;
      }
    }
  }
  if (next > 0)
    instances[context.frameInfo.frameIndex]->write(cpuInstances.data(),
                                                   next * sizeof(Instance));
  useStrands = false;
  if (strandFur && furVisible) {
    std::array<glm::mat4, wildlife::WolfPacks::kWolfCount> models;
    for (uint32_t i = 0; i < next; ++i)
      models[i] = cpuInstances[i].model;
    useStrands = strandFur->prepare(std::span(models).first(next));
  }
}

void WolfSys::updateStudio() {
  studioMode = true;
  std::array<wildlife::WolfPacks::Wolf, wildlife::WolfPacks::kWolfCount>
      wolves{};
  wolves[0].active = true;
  // Cancel the wildlife model's canonical-forward correction: inspect the
  // glTF rest pose at the origin, head toward +Z, with no animation or wind.
  wolves[0].orientation = glm::angleAxis(glm::pi<float>(), glm::vec3(0, 1, 0));
  update(wolves);
}

void WolfSys::rasterizeFur() {
  if (useStrands)
    strandFur->rasterize(scene->sceneTextureSet,
                         instanceSets[context.frameInfo.frameIndex], kPoseCount,
                         jointCount);
}

void WolfSys::renderFur() {
  if (useStrands)
    strandFur->render(scene->sceneTextureSet,
                      instanceSets[context.frameInfo.frameIndex]);
}

bool WolfSys::inFrustum(const std::array<glm::vec4, 6> &planes,
                        uint32_t i) const {
  const glm::vec3 center =
      glm::vec3(cpuInstances[i].model[3]) + glm::vec3(0, 1.1f, 0);
  if (glm::distance(center, context.camera.position) > 500.f)
    return false;
  for (const auto &plane : planes)
    if (glm::dot(glm::vec3(plane), center) + plane.w < -3.5f)
      return false;
  return true;
}

void WolfSys::render(vk::DescriptorSet globalSet, const glm::vec4 &clipPlane,
                     const glm::mat4 *viewProj) {
  const bool reflection = bool(globalSet);
  uint32_t drawnCount = 0;
  const glm::mat4 vp =
      viewProj ? *viewProj
               : context.camera.projectionMatrix * context.camera.viewMatrix;
  const auto planes = terrain::extractFrustumPlanes(vp);
  auto cmd = context.frameInfo.cmd;
  debug::beginLabel(context, cmd, reflection ? "Wolf reflections" : "Wolves",
                    {.7f, .65f, .5f, 1});
  std::array sets{
      globalSet
          ? globalSet
          : context.vulkan.globalDescriptorSets[context.frameInfo.frameIndex],
      scene->sceneTextureSet, instanceSets[context.frameInfo.frameIndex]};
  // The pipelines have identical descriptor/push layouts.
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eGraphics,
                         basePipeline->getLayout(), 0, sets.size(), sets.data(),
                         0, nullptr);
  scene->bind(context, cmd, basePipeline->getLayout());
  for (const auto &group : groups) {
    if (group.count == 0)
      continue;
    const size_t lod = reflection ? 1 : group.lod;
    const auto &mesh = scene->meshes[lodMeshes[lod]];
    basePipeline->bind(cmd);
    Push push{clipPlane,
              {0.f, float(studioMode), float(kPoseCount), float(jointCount)}};
    cmd.pushConstants(basePipeline->getLayout(),
                      vk::ShaderStageFlagBits::eVertex |
                          vk::ShaderStageFlagBits::eFragment,
                      0, sizeof(Push), &push);
    // Batch contiguous visible instances. The same uploaded instance data
    // remains valid for all reflection views; no host overwrites in-flight
    // draw inputs between reflection passes and the main view.
    uint32_t start = group.first;
    const uint32_t end = start + group.count;
    while (start < end) {
      while (start < end && !inFrustum(planes, start))
        ++start;
      uint32_t stop = start;
      while (stop < end && inFrustum(planes, stop))
        ++stop;
      if (stop > start) {
        drawnCount += stop - start;
        const auto &primitive = mesh.primitives[0];
        cmd.drawIndexed(primitive.indexCount, stop - start,
                        primitive.indexOffset, 0, start);
      }
      start = stop;
    }
  }
  if (!reflection &&
      (!reportedStartup || (debugWolves && context.time >= nextDebugReport))) {
    std::println("[wolves] active={} drawn={} nearest={:.1f}m "
                 "position=({:.1f},{:.1f},{:.1f})",
                 activeCount, drawnCount, activeCount ? nearestDistance : -1.f,
                 nearestPosition.x, nearestPosition.y, nearestPosition.z);
    reportedStartup = true;
    nextDebugReport = context.time + 5.f;
  }
  debug::endLabel(context, cmd);
}

} // namespace vkh
