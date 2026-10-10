#include "../src/vkh/systems/wolf/furGroom.hpp"
#include <array>
#include <cassert>
#include <cstring>
#include <fstream>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <iostream>
#include <memory>
#include <vector>
#include <vulkan/vulkan.hpp>

struct Ubo {
  glm::mat4 proj{1}, view{1}, projView{1}, inverseView{1};
  glm::vec2 resolution{64, 64};
  float aspect = 1, time = 0;
  glm::vec4 sunDirection{}, sunColor{}, wind{}, fogParams{}, fogColor{},
      skyAmbient{}, groundAmbient{}, lightGrid{};
  std::array<glm::vec4, 121> lightPositions{}, lightColors{}, lightPaths{};
};
struct Instance {
  glm::mat4 model{1};
  glm::vec4 animation{}, coat{1};
};
struct Push {
  glm::mat4 inverse{1};
  glm::uvec4 window{64, 64, 0, 0}, counts{0, 48, 1, 1};
};
struct Dispatch {
  uint32_t x, y, z, strands, segments;
  float density;
  uint32_t pad[2];
};
static_assert(sizeof(Ubo) == 6208 && sizeof(Instance) == 96 &&
              sizeof(Push) == 96 && sizeof(Dispatch) == 32);
struct Buffer {
  vk::UniqueDeviceMemory memory;
  vk::UniqueBuffer buffer;
  void *mapped = nullptr;
};
std::vector<uint32_t> spirv(const char *file) {
  std::ifstream f(file, std::ios::binary | std::ios::ate);
  assert(f);
  auto bytes = f.tellg();
  f.seekg(0);
  std::vector<uint32_t> code(size_t(bytes) / 4);
  f.read((char *)code.data(), bytes);
  return code;
}
int main() {
  vk::ApplicationInfo ai("fur test", 0, "test", 0, VK_API_VERSION_1_3);
  vk::InstanceCreateInfo ici{};
  ici.pApplicationInfo = &ai;
  auto instance = vk::createInstanceUnique(ici);
  auto physical = instance->enumeratePhysicalDevices().at(0);
  vk::PhysicalDeviceVulkan12Features support12;
  vk::PhysicalDeviceFeatures2 support;
  support.pNext = &support12;
  physical.getFeatures2(&support);
  if (!support.features.shaderInt64 || !support12.shaderBufferInt64Atomics) {
    std::cerr << "Driver does not support 64-bit buffer atomics\n";
    return 77;
  }
  auto families = physical.getQueueFamilyProperties();
  uint32_t family = 0;
  while (!(families[family].queueFlags & vk::QueueFlagBits::eCompute))
    ++family;
  float priority = 1;
  vk::DeviceQueueCreateInfo qci({}, family, 1, &priority);
  vk::PhysicalDeviceVulkan12Features f12;
  f12.shaderBufferInt64Atomics = true;
  vk::PhysicalDeviceFeatures f;
  f.shaderInt64 = true;
  vk::DeviceCreateInfo dci({}, 1, &qci);
  dci.pNext = &f12;
  dci.pEnabledFeatures = &f;
  auto device = physical.createDeviceUnique(dci);
  auto queue = device->getQueue(family, 0);
  auto memoryProperties = physical.getMemoryProperties();
  auto memoryType = [&](uint32_t bits, vk::MemoryPropertyFlags flags) {
    for (uint32_t i = 0; i < memoryProperties.memoryTypeCount; ++i)
      if ((bits & (1u << i)) &&
          (memoryProperties.memoryTypes[i].propertyFlags & flags) == flags)
        return i;
    throw std::runtime_error("No matching memory");
  };
  auto makeBuffer = [&](size_t bytes, vk::BufferUsageFlags usage) {
    auto b = std::make_unique<Buffer>();
    b->buffer =
        device->createBufferUnique(vk::BufferCreateInfo({}, bytes, usage));
    auto req = device->getBufferMemoryRequirements(*b->buffer);
    b->memory = device->allocateMemoryUnique(vk::MemoryAllocateInfo(
        req.size, memoryType(req.memoryTypeBits,
                             vk::MemoryPropertyFlagBits::eHostVisible |
                                 vk::MemoryPropertyFlagBits::eHostCoherent)));
    device->bindBufferMemory(*b->buffer, *b->memory, 0);
    b->mapped = device->mapMemory(*b->memory, 0, bytes);
    std::memset(b->mapped, 0, bytes);
    return b;
  };
  auto storage = vk::BufferUsageFlagBits::eStorageBuffer;
  auto ubo = makeBuffer(sizeof(Ubo), vk::BufferUsageFlagBits::eUniformBuffer);
  auto wolves = makeBuffer(sizeof(Instance), storage);
  auto poses = makeBuffer(sizeof(glm::mat4) * 48, storage);
  auto groom = makeBuffer(sizeof(vkh::FurBundle), storage);
  auto center =
      makeBuffer(4096 * 8, storage | vk::BufferUsageFlagBits::eTransferDst);
  auto conservative =
      makeBuffer(4096 * 8, storage | vk::BufferUsageFlagBits::eTransferDst);
  auto dispatch = makeBuffer(
      sizeof(Dispatch), storage | vk::BufferUsageFlagBits::eIndirectBuffer);
  auto shades = makeBuffer(4096 * 4, storage);
  Ubo global;
  global.sunDirection = {0, 1, 0, 0};
  global.skyAmbient = global.groundAmbient = glm::vec4(.3);
  global.inverseView = glm::translate(glm::mat4(1), glm::vec3(0, 0, 2));
  std::memcpy(ubo->mapped, &global, sizeof(global));
  Instance wolf;
  wolf.animation.y = 1;
  std::memcpy(wolves->mapped, &wolf, sizeof(wolf));
  auto *pose = (glm::mat4 *)poses->mapped;
  for (int i = 0; i < 48; ++i)
    pose[i] = glm::mat4(1);
  vkh::FurBundle bundle{};
  for (int row = 0; row < 4; ++row)
    for (int side = 0; side < 3; ++side) {
      auto &g = bundle.guides[row * 3 + side];
      g.position = {side == 0   ? -.5f
                    : side == 1 ? .5f
                                : 0.f,
                    -.5f + row / 3.f + (side == 2 ? .12f : 0.f), .5f, .5f};
      g.normal = {0, 0, 1, 0};
      g.weights = {1, 0, 0, 0};
    }
  bundle.parameters = {64, .1f, 0, 0};
  std::memcpy(groom->mapped, &bundle, sizeof(bundle));
  vk::ImageCreateInfo imageInfo(
      {}, vk::ImageType::e2D, vk::Format::eR32Sfloat, {64, 64, 1}, 1, 1,
      vk::SampleCountFlagBits::e1, vk::ImageTiling::eOptimal,
      vk::ImageUsageFlagBits::eSampled | vk::ImageUsageFlagBits::eTransferDst);
  auto image = device->createImageUnique(imageInfo);
  auto req = device->getImageMemoryRequirements(*image);
  auto imageMemory = device->allocateMemoryUnique(vk::MemoryAllocateInfo(
      req.size, memoryType(req.memoryTypeBits,
                           vk::MemoryPropertyFlagBits::eDeviceLocal)));
  device->bindImageMemory(*image, *imageMemory, 0);
  auto view = device->createImageViewUnique(vk::ImageViewCreateInfo(
      {}, *image, vk::ImageViewType::e2D, vk::Format::eR32Sfloat, {},
      {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}));
  vk::SamplerCreateInfo si{};
  si.magFilter = si.minFilter = vk::Filter::eNearest;
  auto sampler = device->createSamplerUnique(si);
  auto stage = vk::ShaderStageFlagBits::eCompute;
  vk::DescriptorSetLayoutBinding gb(0, vk::DescriptorType::eUniformBuffer, 1,
                                    stage);
  auto globalLayout = device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo({}, 1, &gb));
  auto emptyLayout = device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo{});
  std::array ib{vk::DescriptorSetLayoutBinding(
                    0, vk::DescriptorType::eStorageBuffer, 1, stage),
                vk::DescriptorSetLayoutBinding(
                    1, vk::DescriptorType::eStorageBuffer, 1, stage)};
  auto instanceLayout = device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo({}, ib));
  std::vector<vk::DescriptorSetLayoutBinding> fb;
  for (uint32_t i = 0; i < 5; ++i)
    fb.emplace_back(i, vk::DescriptorType::eStorageBuffer, 1, stage);
  fb.emplace_back(5, vk::DescriptorType::eCombinedImageSampler, 1, stage);
  fb.emplace_back(6, vk::DescriptorType::eCombinedImageSampler, 1, stage);
  auto furLayout = device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo({}, fb));
  std::array layouts{*globalLayout, *emptyLayout, *instanceLayout, *furLayout};
  vk::PushConstantRange pushRange(stage, 0, sizeof(Push));
  auto pipelineLayout = device->createPipelineLayoutUnique(
      vk::PipelineLayoutCreateInfo({}, layouts, pushRange));
  std::array poolSizes{
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 7),
      vk::DescriptorPoolSize(vk::DescriptorType::eCombinedImageSampler, 2)};
  auto pool = device->createDescriptorPoolUnique(
      vk::DescriptorPoolCreateInfo({}, 4, poolSizes));
  auto sets = device->allocateDescriptorSets(
      vk::DescriptorSetAllocateInfo(*pool, layouts));
  auto writeBuffer = [&](uint32_t set, uint32_t binding, Buffer &b,
                         vk::DescriptorType type) {
    vk::DescriptorBufferInfo bi(*b.buffer, 0, VK_WHOLE_SIZE);
    vk::WriteDescriptorSet write(sets[set], binding, 0, 1, type, nullptr, &bi);
    device->updateDescriptorSets(write, {});
  };
  writeBuffer(0, 0, *ubo, vk::DescriptorType::eUniformBuffer);
  writeBuffer(2, 0, *wolves, vk::DescriptorType::eStorageBuffer);
  writeBuffer(2, 1, *poses, vk::DescriptorType::eStorageBuffer);
  std::array<Buffer *, 5> buffers{groom.get(), center.get(), conservative.get(),
                                  dispatch.get(), shades.get()};
  for (uint32_t i = 0; i < 5; ++i)
    writeBuffer(3, i, *buffers[i], vk::DescriptorType::eStorageBuffer);
  vk::DescriptorImageInfo ii(*sampler, *view,
                             vk::ImageLayout::eShaderReadOnlyOptimal);
  vk::WriteDescriptorSet iw(sets[3], 5, 0, 1,
                            vk::DescriptorType::eCombinedImageSampler, &ii);
  device->updateDescriptorSets(iw, {});
  iw.dstBinding = 6;
  device->updateDescriptorSets(iw, {});
  auto makePipeline = [&](const char *path) {
    auto code = spirv(path);
    auto module = device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, code.size() * 4, code.data()));
    vk::PipelineShaderStageCreateInfo sci({}, stage, *module, "main");
    return device
        ->createComputePipelineUnique(
            {}, vk::ComputePipelineCreateInfo({}, sci, *pipelineLayout))
        .value;
  };
  auto lod = makePipeline("build/shaders/wolf/furLod.comp.spv");
  auto raster = makePipeline("build/shaders/wolf/furRaster.comp.spv");
  auto shade = makePipeline("build/shaders/wolf/furShade.comp.spv");
  auto commandPool = device->createCommandPoolUnique(vk::CommandPoolCreateInfo(
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family));
  auto cmds =
      device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
          *commandPool, vk::CommandBufferLevel::ePrimary, 1));
  auto cmd = *cmds[0];
  bool first = true;
  auto run = [&](float depth) {
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    vk::ImageMemoryBarrier imageBarrier{};
    imageBarrier.srcAccessMask =
        first ? vk::AccessFlags{} : vk::AccessFlagBits::eShaderRead;
    imageBarrier.dstAccessMask = vk::AccessFlagBits::eTransferWrite;
    imageBarrier.oldLayout = first ? vk::ImageLayout::eUndefined
                                   : vk::ImageLayout::eShaderReadOnlyOptimal;
    imageBarrier.newLayout = vk::ImageLayout::eTransferDstOptimal;
    imageBarrier.srcQueueFamilyIndex = imageBarrier.dstQueueFamilyIndex =
        VK_QUEUE_FAMILY_IGNORED;
    imageBarrier.image = *image;
    imageBarrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0,
                                     1};
    cmd.pipelineBarrier(first ? vk::PipelineStageFlagBits::eTopOfPipe
                              : vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eTransfer, {}, {}, {},
                        imageBarrier);
    cmd.clearColorImage(
        *image, vk::ImageLayout::eTransferDstOptimal,
        vk::ClearColorValue(std::array<float, 4>{depth, 0, 0, 0}),
        imageBarrier.subresourceRange);
    imageBarrier.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
    imageBarrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
    imageBarrier.oldLayout = vk::ImageLayout::eTransferDstOptimal;
    imageBarrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                        vk::PipelineStageFlagBits::eComputeShader, {}, {}, {},
                        imageBarrier);
    cmd.fillBuffer(*center->buffer, 0, VK_WHOLE_SIZE, ~0u);
    cmd.fillBuffer(*conservative->buffer, 0, VK_WHOLE_SIZE, ~0u);
    vk::MemoryBarrier clear(
        vk::AccessFlagBits::eTransferWrite | vk::AccessFlagBits::eHostWrite,
        vk::AccessFlagBits::eShaderRead | vk::AccessFlagBits::eShaderWrite);
    cmd.pipelineBarrier(
        vk::PipelineStageFlagBits::eTransfer | vk::PipelineStageFlagBits::eHost,
        vk::PipelineStageFlagBits::eComputeShader, {}, clear, {}, {});
    Push push;
    push.counts.x = 1;
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *pipelineLayout, 0,
                           sets, {});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *lod);
    cmd.pushConstants(*pipelineLayout, stage, 0, sizeof(push), &push);
    cmd.dispatch(1, 1, 1);
    vk::MemoryBarrier indirect(vk::AccessFlagBits::eShaderWrite,
                               vk::AccessFlagBits::eIndirectCommandRead |
                                   vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eDrawIndirect |
                            vk::PipelineStageFlagBits::eComputeShader,
                        {}, indirect, {}, {});
    push.counts.x = 0;
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *raster);
    cmd.pushConstants(*pipelineLayout, stage, 0, sizeof(push), &push);
    cmd.dispatchIndirect(*dispatch->buffer, 0);
    vk::MemoryBarrier shadeReady(vk::AccessFlagBits::eShaderWrite,
                                 vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eComputeShader, {},
                        shadeReady, {}, {});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, *shade);
    cmd.dispatch(8, 8, 1);
    vk::MemoryBarrier host(vk::AccessFlagBits::eShaderWrite,
                           vk::AccessFlagBits::eHostRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eHost, {}, host, {}, {});
    cmd.end();
    vk::SubmitInfo submit({}, {}, cmd);
    queue.submit(submit);
    queue.waitIdle();
    first = false;
    auto *c = (uint64_t *)center->mapped, *g = (uint64_t *)conservative->mapped;
    size_t centers = 0, coverage = 0;
    for (int i = 0; i < 4096; ++i) {
      centers += c[i] != ~uint64_t(0);
      coverage += g[i] != ~uint64_t(0);
    }
    return std::array{centers, coverage};
  };
  auto visible = run(.2);
  assert(visible[0] > 0 && visible[1] >= visible[0]);
  assert(((uint32_t *)shades->mapped)[0] == 0);
  size_t shaded = 0;
  for (int i = 0; i < 4096; ++i)
    shaded += ((uint32_t *)shades->mapped)[i] != 0;
  assert(shaded == visible[0]);
  std::vector<uint64_t> reference((uint64_t *)conservative->mapped,
                                  (uint64_t *)conservative->mapped + 4096);
  auto repeat = run(.2);
  assert(visible == repeat);
  assert(std::memcmp(reference.data(), conservative->mapped, 4096 * 8) == 0);
  auto hidden = run(1);
  assert(hidden[0] == 0 && hidden[1] == 0);
  global.wind = {1, 0, 8, 1};
  global.time = .7;
  std::memcpy(ubo->mapped, &global, sizeof(global));
  auto wind = run(.2);
  assert(wind[1] > 0);
  assert(std::memcmp(reference.data(), conservative->mapped, 4096 * 8) != 0);
  global.wind = glm::vec4(0);
  global.time = 0;
  std::memcpy(ubo->mapped, &global, sizeof(global));
  wolf.animation.z = 1;
  std::memcpy(wolves->mapped, &wolf, sizeof(wolf));
  for (int i = 0; i < 48; ++i)
    pose[i] = glm::translate(glm::mat4(1), glm::vec3(.15f, 0, 0));
  auto animated = run(.2);
  assert(animated[1] > 0);
  assert(std::memcmp(reference.data(), conservative->mapped, 4096 * 8) != 0);
  wolf.animation.z = 0;
  std::memcpy(wolves->mapped, &wolf, sizeof(wolf));
  global.view = glm::translate(glm::mat4(1), glm::vec3(0, 0, -100));
  std::memcpy(ubo->mapped, &global, sizeof(global));
  run(0);
  auto reduced = *(Dispatch *)dispatch->mapped;
  assert(reduced.strands < 64 && reduced.segments == 2 && reduced.y == 1);
  bundle.guides[0].position.z = bundle.guides[1].position.z = -.1f;
  std::memcpy(groom->mapped, &bundle, sizeof(bundle));
  const auto clipped = run(0);
  assert(clipped[1] > 0 && clipped[1] < 4096);
  std::cout
      << physical.getProperties().deviceName << ": " << visible[0]
      << " center pixels, " << visible[1]
      << " conservative pixels; repeat, depth rejection, wind, animation, "
         "LOD, near clipping, deferred shading passed\n";
  for (auto *b : buffers)
    device->unmapMemory(*b->memory);
  device->unmapMemory(*ubo->memory);
  device->unmapMemory(*wolves->memory);
  device->unmapMemory(*poses->memory);
  view.reset();
  image.reset();
  imageMemory.reset();
}
