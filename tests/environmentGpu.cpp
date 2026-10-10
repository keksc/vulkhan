#include "../src/vkh/streamLightCache.hpp"
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
static_assert(sizeof(Ubo) == 6208);
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
  vk::ApplicationInfo ai("environment test", 0, "test", 0, VK_API_VERSION_1_3);
  vk::InstanceCreateInfo ici{};
  ici.pApplicationInfo = &ai;
  auto instance = vk::createInstanceUnique(ici);
  auto physical = instance->enumeratePhysicalDevices().at(0);
  auto families = physical.getQueueFamilyProperties();
  uint32_t family = 0;
  while (!(families[family].queueFlags & vk::QueueFlagBits::eCompute))
    ++family;
  float priority = 1;
  vk::DeviceQueueCreateInfo qci({}, family, 1, &priority);
  vk::DeviceCreateInfo dci({}, 1, &qci);
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
  struct Vertex {
    glm::vec4 pos, col, params;
  };
  static_assert(sizeof(Vertex) == 48);
  auto ubo = makeBuffer(sizeof(Ubo), vk::BufferUsageFlagBits::eUniformBuffer);
  auto output = makeBuffer(65536 * sizeof(Vertex),
                           vk::BufferUsageFlagBits::eStorageBuffer);
  auto transforms = makeBuffer(41 * sizeof(glm::mat4),
                               vk::BufferUsageFlagBits::eStorageBuffer);
  auto stage = vk::ShaderStageFlagBits::eCompute;
  vk::DescriptorSetLayoutBinding gb(0, vk::DescriptorType::eUniformBuffer, 1,
                                    stage);
  auto globalLayout = device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo({}, 1, &gb));
  std::array bindings{vk::DescriptorSetLayoutBinding(
                          0, vk::DescriptorType::eStorageBuffer, 1, stage),
                      vk::DescriptorSetLayoutBinding(
                          1, vk::DescriptorType::eStorageBuffer, 1, stage)};
  auto particleLayout = device->createDescriptorSetLayoutUnique(
      vk::DescriptorSetLayoutCreateInfo({}, bindings));
  std::array layouts{*globalLayout, *particleLayout};
  vk::PushConstantRange range(stage, 0, 16);
  auto layout = device->createPipelineLayoutUnique(
      vk::PipelineLayoutCreateInfo({}, layouts, range));
  std::array sizes{
      vk::DescriptorPoolSize(vk::DescriptorType::eUniformBuffer, 1),
      vk::DescriptorPoolSize(vk::DescriptorType::eStorageBuffer, 2)};
  auto pool = device->createDescriptorPoolUnique(
      vk::DescriptorPoolCreateInfo({}, 2, sizes));
  auto sets = device->allocateDescriptorSets(
      vk::DescriptorSetAllocateInfo(*pool, layouts));
  auto bind = [&](int set, int binding, Buffer &buffer,
                  vk::DescriptorType type) {
    vk::DescriptorBufferInfo bi(*buffer.buffer, 0, VK_WHOLE_SIZE);
    device->updateDescriptorSets(
        vk::WriteDescriptorSet(sets[set], binding, 0, 1, type, nullptr, &bi),
        {});
  };
  bind(0, 0, *ubo, vk::DescriptorType::eUniformBuffer);
  bind(1, 0, *output, vk::DescriptorType::eStorageBuffer);
  bind(1, 1, *transforms, vk::DescriptorType::eStorageBuffer);
  auto pipeline = [&](const char *path) {
    auto code = spirv(path);
    auto module = device->createShaderModuleUnique(
        vk::ShaderModuleCreateInfo({}, code.size() * 4, code.data()));
    return device
        ->createComputePipelineUnique(
            {}, vk::ComputePipelineCreateInfo({},
                                              vk::PipelineShaderStageCreateInfo(
                                                  {}, stage, *module, "main"),
                                              *layout))
        .value;
  };
  auto field = pipeline("/tmp/environmentField.spv");
  auto particles = pipeline("build/shaders/particles.comp.spv");
  auto commandPool = device->createCommandPoolUnique(vk::CommandPoolCreateInfo(
      vk::CommandPoolCreateFlagBits::eResetCommandBuffer, family));
  auto commands =
      device->allocateCommandBuffersUnique(vk::CommandBufferAllocateInfo(
          *commandPool, vk::CommandBufferLevel::ePrimary, 1));
  auto cmd = *commands[0];
  Ubo global;
  struct ParticlePush {
    uint32_t count = 0, patches = 1, particles = 6344;
    float time = 2;
  } push;
  auto run = [&](vk::Pipeline p, uint32_t groups) {
    std::memcpy(ubo->mapped, &global, sizeof(global));
    cmd.reset();
    cmd.begin(vk::CommandBufferBeginInfo(
        vk::CommandBufferUsageFlagBits::eOneTimeSubmit));
    vk::MemoryBarrier upload(vk::AccessFlagBits::eHostWrite,
                             vk::AccessFlagBits::eShaderRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eHost,
                        vk::PipelineStageFlagBits::eComputeShader, {}, upload,
                        {}, {});
    cmd.bindPipeline(vk::PipelineBindPoint::eCompute, p);
    cmd.bindDescriptorSets(vk::PipelineBindPoint::eCompute, *layout, 0, sets,
                           {});
    cmd.pushConstants(*layout, stage, 0, sizeof(push), &push);
    cmd.dispatch(groups, 1, 1);
    vk::MemoryBarrier download(vk::AccessFlagBits::eShaderWrite,
                               vk::AccessFlagBits::eHostRead);
    cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                        vk::PipelineStageFlagBits::eHost, {}, download, {}, {});
    cmd.end();
    queue.submit(vk::SubmitInfo({}, {}, cmd), {});
    queue.waitIdle();
  };
  auto *values = static_cast<Vertex *>(output->mapped);
  run(*field, 256);
  std::vector<Vertex> reference(values, values + 65536);
  float lo = 100, hi = 0, maxDelta = 0, maxRelative = 0;
  int darkHabitats = 0;
  for (auto v : reference) {
    assert(std::isfinite(v.pos.x) && v.pos.x > 0);
    float delta = glm::length(glm::vec3(v.pos - v.col));
    maxDelta = std::max(maxDelta, delta);
    maxRelative = std::max(maxRelative, delta / glm::length(glm::vec3(v.pos)));
    assert(v.params.x > v.params.y * 3.f);
    assert(v.params.z < v.params.x * .2f);
    darkHabitats += v.pos.x < .035f;
    lo = std::min(lo, v.pos.x);
    hi = std::max(hi, v.pos.x);
  }
  std::cout << "Field millimeter-step max delta=" << maxDelta
            << ", relative=" << maxRelative << ", contrast=" << hi / lo
            << ", dark cells=" << darkHabitats << "\n";
  // Directional irradiance has a steeper legitimate gradient than flat fill.
  // A millimeter step must still change energy by less than 0.2 percent.
  assert(maxRelative < .002f);
  assert(hi / lo > 15.f);
  assert(darkHabitats > 65536 / 4);
  global.time = 10000;
  global.inverseView[3] = {10000, 200, 5000, 1};
  global.lightGrid = glm::vec4(100);
  global.lightColors.fill(glm::vec4(500));
  run(*field, 256);
  assert(std::memcmp(reference.data(), values,
                     reference.size() * sizeof(Vertex)) == 0);
  std::ofstream preview("/tmp/environment-habitats.ppm", std::ios::binary);
  preview << "P6\n256 256\n255\n";
  for (auto v : reference)
    for (int c = 0; c < 3; ++c) {
      unsigned char value = static_cast<unsigned char>(
          glm::clamp(std::pow(v.pos[c], 1.f / 2.2f) * 255.f, 0.f, 255.f));
      preview.write(reinterpret_cast<char *>(&value), 1);
    }
  preview.close();
  global = Ubo{};
  global.wind = {1, .3, 6, .65};
  vkh::StreamLightCache cache;
  vkh::NightLights lights;
  for (int i = 0; i < 100; ++i)
    lights = cache.update(
        {}, [](glm::vec2) { return 0.f; }, [](glm::vec2) { return false; },
        i * .02f);
  global.lightPositions = lights.positions;
  global.lightColors = lights.colors;
  global.lightPaths = lights.paths;
  auto *mat = static_cast<glm::mat4 *>(transforms->mapped);
  for (int i = 0; i < 41; ++i)
    mat[i] = glm::mat4(1);
  run(*particles, 25);
  std::vector<Vertex> initial(values, values + 6344);
  int near = 0, middle = 0, far = 0, petals = 0, flowers = 0;
  for (int i = 0; i < 6344; ++i) {
    auto v = values[i];
    assert(std::isfinite(v.pos.x) && std::isfinite(v.pos.y));
    if (v.col.w > .001f) {
      if (i < 49 * 64)
        ++near;
      else if (i < 74 * 64)
        ++middle;
      else if (i < 6336)
        ++far;
      petals += v.params.x == 1;
      flowers += v.params.x == 2;
    }
  }
  assert(near > 100 && middle > 10 && far > 5 && petals > 10 && flowers == 1);
  run(*particles, 25);
  assert(std::memcmp(initial.data(), values, initial.size() * sizeof(Vertex)) ==
         0);
  push.time += .001f;
  run(*particles, 25);
  for (int i = 0; i < 6344; ++i)
    assert(glm::length(initial[i].pos - values[i].pos) < .01f);
  std::cout << "GPU field continuous and camera/time/residency independent; "
               "particle counts near/middle/far="
            << near << "/" << middle << "/" << far
            << "; stable motion and flower/petal types passed\n";
  device->unmapMemory(*ubo->memory);
  device->unmapMemory(*output->memory);
  device->unmapMemory(*transforms->memory);
}
