#include "sky.hpp"

#include "../buffer.hpp"
#include "../debug.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../paths.hpp"
#include "../pipeline.hpp"
#include "../sceneBuilder.hpp"
#include <chrono>
#include <filesystem>
#include <format>
#include <fstream>
#include <print>
#include <vector>
#include <vulkan/vulkan.hpp>
#include <vulkan/vulkan_core.h>

namespace vkh {

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  glm::mat4 normalMatrix{1.f};
};

namespace {

// Resolution-tagged cache filenames: lets caches for multiple resolutions
// coexist on disk instead of one clobbering the other every time the user
// switches resolution back and forth.
std::filesystem::path milkyWayCachePath(uint32_t faceSize) {
  return paths::cacheDir() / std::format("sky_milkyway_{}.bin", faceSize);
}
std::filesystem::path discCachePath(uint32_t texSize) {
  return paths::cacheDir() / std::format("sky_disc_{}.bin", texSize);
}

// Human-readable byte count, since bake sizes span KB (disc turbulence) to
// several MB (milky way cubemap at high resolution) -- makes cache-size
// log lines actually legible at a glance instead of a wall of digits.
std::string formatBytes(vk::DeviceSize bytes) {
  constexpr double kMiB = 1024.0 * 1024.0;
  if (bytes >= static_cast<vk::DeviceSize>(kMiB))
    return std::format("{:.1f} MiB", static_cast<double>(bytes) / kMiB);
  return std::format("{:.1f} KiB", static_cast<double>(bytes) / 1024.0);
}

} // namespace

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

// Tries to restore milkyWayCubemap/discTurbulence from the on-disk cache
// written by saveBakedToCache(). See sky.hpp for the exact contract.
bool SkySys::tryLoadBakedFromCache() {
  if (!useCachedBake) {
    std::println("sky: cache disabled (useCachedSkyBake=false), baking "
                 "{}x{} milky way / {}x{} disc from scratch",
                 milkyWayFaceSize, milkyWayFaceSize, discTurbulenceSize,
                 discTurbulenceSize);
    return false;
  }

  auto loadStart = std::chrono::steady_clock::now();

  const vk::DeviceSize milkyWayBytes =
      vk::DeviceSize{milkyWayFaceSize} * milkyWayFaceSize * 6 *
      Image::getFormatSize(vk::Format::eR16G16B16A16Sfloat);
  const vk::DeviceSize discBytes =
      vk::DeviceSize{discTurbulenceSize} * discTurbulenceSize *
      Image::getFormatSize(vk::Format::eR8Unorm);

  const auto milkyWayPath = milkyWayCachePath(milkyWayFaceSize);
  const auto discPath = discCachePath(discTurbulenceSize);

  std::error_code ec;
  bool milkyWayExists = std::filesystem::exists(milkyWayPath, ec);
  bool discExists = !ec && std::filesystem::exists(discPath, ec);
  if (!milkyWayExists || !discExists) {
    std::println("sky: no cache for {}x{}/{}x{} at {} -- will bake fresh",
                 milkyWayFaceSize, milkyWayFaceSize, discTurbulenceSize,
                 discTurbulenceSize, paths::cacheDir().string());
    return false;
  }

  auto milkyWaySizeOnDisk = std::filesystem::file_size(milkyWayPath, ec);
  if (ec || milkyWaySizeOnDisk != milkyWayBytes) {
    std::println("sky: milky way cache size mismatch (expected {}, found "
                 "{}) -- cache is stale or corrupt, rebaking",
                 formatBytes(milkyWayBytes),
                 ec ? "unreadable" : formatBytes(milkyWaySizeOnDisk));
    return false;
  }
  auto discSizeOnDisk = std::filesystem::file_size(discPath, ec);
  if (ec || discSizeOnDisk != discBytes) {
    std::println("sky: disc turbulence cache size mismatch (expected {}, "
                 "found {}) -- cache is stale or corrupt, rebaking",
                 formatBytes(discBytes),
                 ec ? "unreadable" : formatBytes(discSizeOnDisk));
    return false;
  }

  std::vector<unsigned char> milkyWayPixels(milkyWayBytes);
  std::vector<unsigned char> discPixels(discBytes);
  {
    std::ifstream milkyWayFile(milkyWayPath, std::ios::binary);
    std::ifstream discFile(discPath, std::ios::binary);
    if (!milkyWayFile || !discFile) {
      std::println("sky: cache file(s) present but could not be opened for "
                   "reading, falling back to a fresh bake");
      return false;
    }
    milkyWayFile.read(reinterpret_cast<char *>(milkyWayPixels.data()),
                      milkyWayBytes);
    discFile.read(reinterpret_cast<char *>(discPixels.data()), discBytes);
    if (!milkyWayFile || !discFile) {
      std::println("sky: cache read failed partway through (disk error or "
                   "truncated file), falling back to a fresh bake");
      return false;
    }
  }

  // Recreate the target images exactly as the compute-bake path would.
  ImageCreateInfo_cubemapStorage cubemapInfo{};
  cubemapInfo.faceSize = milkyWayFaceSize;
  cubemapInfo.format = vk::Format::eR16G16B16A16Sfloat;
  cubemapInfo.usage = vk::ImageUsageFlagBits::eStorage |
                      vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eTransferDst;
  cubemapInfo.layout = vk::ImageLayout::eGeneral;
  cubemapInfo.name = "milky way cubemap (cached)";
  auto cachedMilkyWay = std::make_unique<Cubemap>(context, cubemapInfo);

  ImageCreateInfo_empty discInfo{};
  discInfo.size = {discTurbulenceSize, discTurbulenceSize};
  discInfo.format = vk::Format::eR8Unorm;
  discInfo.usage = vk::ImageUsageFlagBits::eStorage |
                   vk::ImageUsageFlagBits::eSampled |
                   vk::ImageUsageFlagBits::eTransferDst;
  discInfo.layout = vk::ImageLayout::eGeneral;
  discInfo.name = "disc turbulence texture (cached)";
  auto cachedDisc = std::make_unique<Image2D>(context, discInfo);

  // Host-visible source buffers for the upload -- same pattern the
  // ImageCreateInfo_data constructor uses for its own staging buffer.
  Buffer<std::byte> milkyWayStaging(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      milkyWayBytes);
  milkyWayStaging.map();
  milkyWayStaging.write(milkyWayPixels.data());

  Buffer<std::byte> discStaging(
      context, vk::BufferUsageFlagBits::eTransferSrc,
      vk::MemoryPropertyFlagBits::eHostVisible |
          vk::MemoryPropertyFlagBits::eHostCoherent,
      discBytes);
  discStaging.map();
  discStaging.write(discPixels.data());

  auto cmd = beginSingleTimeCommands(context);

  cachedMilkyWay->recordTransitionLayout(
      cmd, vk::ImageLayout::eTransferDstOptimal,
      vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6});
  cachedMilkyWay->recordCopyFromBuffer(cmd, milkyWayStaging, 0, 0, 6);
  cachedMilkyWay->recordTransitionLayout(
      cmd, vk::ImageLayout::eShaderReadOnlyOptimal,
      vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 6});

  cachedDisc->recordTransitionLayout(cmd,
                                     vk::ImageLayout::eTransferDstOptimal);
  cachedDisc->recordCopyFromBuffer(cmd, discStaging);
  cachedDisc->recordTransitionLayout(cmd,
                                     vk::ImageLayout::eShaderReadOnlyOptimal);

  endSingleTimeCommands(context, cmd, context.vulkan.graphicsQueue);

  milkyWayCubemap = std::move(cachedMilkyWay);
  discTurbulence = std::move(cachedDisc);

  auto elapsedMs = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - loadStart)
                       .count();
  std::println("sky: loaded baked environment from cache in {:.1f} ms "
               "({} milky way + {} disc, {}x{}/{}x{})",
               elapsedMs, formatBytes(milkyWayBytes), formatBytes(discBytes),
               milkyWayFaceSize, milkyWayFaceSize, discTurbulenceSize,
               discTurbulenceSize);
  return true;
}

// Downloads milkyWayCubemap/discTurbulence to disk. See sky.hpp for the
// exact contract (images must already hold their final baked contents).
void SkySys::saveBakedToCache() {
  auto saveStart = std::chrono::steady_clock::now();

  const vk::DeviceSize milkyWayBytes =
      vk::DeviceSize{milkyWayFaceSize} * milkyWayFaceSize * 6 *
      Image::getFormatSize(vk::Format::eR16G16B16A16Sfloat);
  const vk::DeviceSize discBytes =
      vk::DeviceSize{discTurbulenceSize} * discTurbulenceSize *
      Image::getFormatSize(vk::Format::eR8Unorm);

  std::vector<unsigned char> milkyWayPixels(milkyWayBytes);
  milkyWayCubemap->downloadPixels(milkyWayPixels.data(), /*mipLevel=*/0,
                                  /*baseArrayLayer=*/0, /*layerCount=*/6);

  std::vector<unsigned char> discPixels(discBytes);
  discTurbulence->downloadPixels(discPixels.data(), /*mipLevel=*/0);

  std::error_code ec;
  std::filesystem::create_directories(paths::cacheDir(), ec);

  std::ofstream milkyWayFile(milkyWayCachePath(milkyWayFaceSize),
                            std::ios::binary | std::ios::trunc);
  milkyWayFile.write(reinterpret_cast<const char *>(milkyWayPixels.data()),
                     static_cast<std::streamsize>(milkyWayPixels.size()));

  std::ofstream discFile(discCachePath(discTurbulenceSize),
                         std::ios::binary | std::ios::trunc);
  discFile.write(reinterpret_cast<const char *>(discPixels.data()),
                 static_cast<std::streamsize>(discPixels.size()));

  if (!milkyWayFile || !discFile) {
    std::println("sky: failed to write bake cache to {} -- next launch at "
                 "this resolution will re-bake from scratch",
                 paths::cacheDir().string());
    return;
  }

  auto elapsedMs = std::chrono::duration<double, std::milli>(
                       std::chrono::steady_clock::now() - saveStart)
                       .count();
  std::println("sky: cached baked environment in {:.1f} ms ({} milky way + "
               "{} disc) -> {}",
               elapsedMs, formatBytes(milkyWayBytes), formatBytes(discBytes),
               paths::cacheDir().string());
}

// One-time compute bake of the milky way cubemap and accretion-disc
// turbulence texture. Both are static (no runtime dependency on time,
// camera, etc.), so this only ever runs once per resolution - the black
// hole shader's actual gravitational lensing stays real-time in sky.frag,
// only its two "texture channels" (originally iChannel0/iChannel1 in the
// Shadertoy source) are baked ahead of time instead of computed per pixel
// per frame.
//
// If a valid cache exists on disk for the current milkyWayFaceSize/
// discTurbulenceSize and useCachedBake is true (see
// tryLoadBakedFromCache()/saveBakedToCache()), the compute dispatch below
// is skipped entirely and the images are just uploaded from disk instead,
// which is significantly cheaper at startup.
void SkySys::bakeEnvironment() {
  if (tryLoadBakedFromCache())
    return;

  auto bakeStart = std::chrono::steady_clock::now();
  std::println("sky: dispatching compute bake at {}x{} milky way / {}x{} "
               "disc turbulence...",
               milkyWayFaceSize, milkyWayFaceSize, discTurbulenceSize,
               discTurbulenceSize);

  // ---- create the target images (storage-writable, then sampled) ----
  ImageCreateInfo_cubemapStorage cubemapInfo{};
  cubemapInfo.faceSize = milkyWayFaceSize;
  cubemapInfo.format = vk::Format::eR16G16B16A16Sfloat;
  cubemapInfo.usage = vk::ImageUsageFlagBits::eStorage |
                      vk::ImageUsageFlagBits::eSampled |
                      vk::ImageUsageFlagBits::eTransferSrc;
  cubemapInfo.layout = vk::ImageLayout::eGeneral;
  cubemapInfo.name = "milky way cubemap";
  milkyWayCubemap = std::make_unique<Cubemap>(context, cubemapInfo);

  ImageCreateInfo_empty discInfo{};
  discInfo.size = {discTurbulenceSize, discTurbulenceSize};
  discInfo.format = vk::Format::eR8Unorm;
  discInfo.usage = vk::ImageUsageFlagBits::eStorage |
                   vk::ImageUsageFlagBits::eSampled |
                   vk::ImageUsageFlagBits::eTransferSrc;
  discInfo.layout = vk::ImageLayout::eGeneral;
  discInfo.name = "disc turbulence texture";
  discTurbulence = std::make_unique<Image2D>(context, discInfo);

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
  MilkyWayPush milkyWayPush{milkyWayFaceSize};
  cmd.pushConstants(milkyWayPipeline.getLayout(),
                    vk::ShaderStageFlagBits::eCompute, 0, sizeof(MilkyWayPush),
                    &milkyWayPush);
  uint32_t milkyWayGroups = (milkyWayFaceSize + 7) / 8;
  cmd.dispatch(milkyWayGroups, milkyWayGroups, 6);

  discPipeline.bind(cmd);
  cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute,
                         discPipeline.getLayout(), 0, 1, &discBakeSet, 0,
                         nullptr);
  DiscPush discPush{discTurbulenceSize};
  cmd.pushConstants(discPipeline.getLayout(), vk::ShaderStageFlagBits::eCompute,
                    0, sizeof(DiscPush), &discPush);
  uint32_t discGroups = (discTurbulenceSize + 7) / 8;
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

  auto bakeElapsedMs = std::chrono::duration<double, std::milli>(
                          std::chrono::steady_clock::now() - bakeStart)
                          .count();
  std::println("sky: compute bake finished in {:.1f} ms (GPU-side; "
               "includes the vkQueueWaitIdle inside endSingleTimeCommands)",
               bakeElapsedMs);

  saveBakedToCache();
}

void SkySys::writeBakedDescriptorSet() {
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

SkySys::SkySys(EngineContext &context, uint32_t milkyWayFaceSize,
               uint32_t discTurbulenceSize, bool useCachedBake)
    : System(context), useCachedBake(useCachedBake),
      milkyWayFaceSize(milkyWayFaceSize),
      discTurbulenceSize(discTurbulenceSize) {
  bakeEnvironment();
  createBakedDescriptorSetLayout();

  bakedSet = context.vulkan.globalDescriptorAllocator->allocate(bakedSetLayout);
  writeBakedDescriptorSet();

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

// Re-bakes at a new resolution, replacing the live environment. The
// pipeline and bakedSetLayout don't depend on resolution, so only the
// images + their descriptor writes need to be redone -- same shape as
// the constructor's bake step, minus the one-time pipeline/layout setup.
void SkySys::rebake(uint32_t newMilkyWayFaceSize,
                    uint32_t newDiscTurbulenceSize) {
  if (newMilkyWayFaceSize == milkyWayFaceSize &&
      newDiscTurbulenceSize == discTurbulenceSize) {
    std::println("sky: rebake requested at {}x{}/{}x{}, already at that "
                 "resolution -- skipping",
                 newMilkyWayFaceSize, newMilkyWayFaceSize,
                 newDiscTurbulenceSize, newDiscTurbulenceSize);
    return; // no-op, avoid an unnecessary stall + rebake
  }

  std::println("sky: live rebake {}x{}/{}x{} -> {}x{}/{}x{}", milkyWayFaceSize,
               milkyWayFaceSize, discTurbulenceSize, discTurbulenceSize,
               newMilkyWayFaceSize, newMilkyWayFaceSize, newDiscTurbulenceSize,
               newDiscTurbulenceSize);

  auto rebakeStart = std::chrono::steady_clock::now();

  // Old images may still be read by an in-flight frame's descriptor set
  // (bakedSet), so make sure the GPU is done with them before tearing
  // anything down.
  context.vulkan.device.waitIdle();

  milkyWayFaceSize = newMilkyWayFaceSize;
  discTurbulenceSize = newDiscTurbulenceSize;

  milkyWayCubemap.reset();
  discTurbulence.reset();

  bakeEnvironment(); // checks the (now-updated) cache path/size first,
                     // falls back to a fresh compute bake if no match; logs
                     // its own timing/size details above

  writeBakedDescriptorSet(); // bakedSet itself is reused -- same layout,
                             // just re-pointed at the new images

  auto totalMs = std::chrono::duration<double, std::milli>(
                    std::chrono::steady_clock::now() - rebakeStart)
                    .count();
  std::println("sky: rebake complete, {:.1f} ms total (includes the "
               "waitIdle stall above)",
               totalMs);
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
