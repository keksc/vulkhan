#include "wolfStudio.hpp"

#include "../vkh/buffer.hpp"
#include "../vkh/camera.hpp"
#include "../vkh/cleanup.hpp"
#include "../vkh/descriptors.hpp"
#include "../vkh/init.hpp"
#include "../vkh/renderer.hpp"
#include "../vkh/swapChain.hpp"
#include "../vkh/systems/postProcessing.hpp"
#include "../vkh/systems/wolf/wolfSys.hpp"
#include "../vkh/window.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/quaternion.hpp>
#ifdef _WIN32
#include <stb_image_write.h>
#else
#include <stb/stb_image_write.h>
#endif

#include <array>
#include <cmath>
#include <cstdlib>
#include <print>

namespace app {
namespace {
// Studio uses the engine's actual body, strand rasterizer and compositor.
// This final pass only converts linear light to the display's sRGB encoding.
class StudioDisplay : public vkh::ComputeEffect<glm::vec4> {
public:
  std::vector<vk::DescriptorSetLayoutBinding>
  additionalBindings() const override {
    return {};
  }
  void writeAdditionalDescriptors(vkh::DescriptorWriter &, uint32_t) override {}
  const char *shaderPath() const override {
    return "shaders/wolf/studio.comp.spv";
  }
  const char *debugName() const override { return "wolf studio display"; }
  glm::vec4 makePushConstants() const override { return glm::vec4(1); }
};

void recordCapture(vkh::EngineContext &context,
                   vkh::Buffer<uint8_t> &readback) {
  auto cmd = context.frameInfo.cmd;
  auto image = context.vulkan.swapChain->getSceneColorImage(
      vkh::renderer::getCurrentImageIndex());
  vk::ImageMemoryBarrier barrier{};
  barrier.srcAccessMask = vk::AccessFlagBits::eShaderRead;
  barrier.dstAccessMask = vk::AccessFlagBits::eTransferRead;
  // The final display effect sampled this image and leaves it shader-read
  // only for the rest of the frame.
  barrier.oldLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  barrier.newLayout = vk::ImageLayout::eTransferSrcOptimal;
  barrier.srcQueueFamilyIndex = barrier.dstQueueFamilyIndex =
      vk::QueueFamilyIgnored;
  barrier.image = image;
  barrier.subresourceRange = {vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1};
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eComputeShader,
                      vk::PipelineStageFlagBits::eTransfer, {}, 0, nullptr, 0,
                      nullptr, 1, &barrier);
  vk::BufferImageCopy region{};
  region.imageSubresource = {vk::ImageAspectFlagBits::eColor, 0, 0, 1};
  region.imageExtent = vk::Extent3D{uint32_t(context.window.size.x),
                                    uint32_t(context.window.size.y), 1};
  cmd.copyImageToBuffer(image, vk::ImageLayout::eTransferSrcOptimal, readback,
                        1, &region);
  barrier.oldLayout = vk::ImageLayout::eTransferSrcOptimal;
  barrier.newLayout = vk::ImageLayout::eShaderReadOnlyOptimal;
  barrier.srcAccessMask = vk::AccessFlagBits::eTransferRead;
  barrier.dstAccessMask = vk::AccessFlagBits::eShaderRead;
  vk::BufferMemoryBarrier host{};
  host.srcAccessMask = vk::AccessFlagBits::eTransferWrite;
  host.dstAccessMask = vk::AccessFlagBits::eHostRead;
  host.srcQueueFamilyIndex = host.dstQueueFamilyIndex = vk::QueueFamilyIgnored;
  host.buffer = readback;
  host.size = vk::WholeSize;
  cmd.pipelineBarrier(vk::PipelineStageFlagBits::eTransfer,
                      vk::PipelineStageFlagBits::eComputeShader |
                          vk::PipelineStageFlagBits::eHost,
                      {}, 0, nullptr, 1, &host, 1, &barrier);
}

void saveCapture(vkh::EngineContext &context, vkh::Buffer<uint8_t> &readback,
                 const char *path) {
  context.vulkan.device.waitIdle();
  readback.map();
  auto *bytes = static_cast<uint8_t *>(readback.getMappedAddr());
  const auto format = context.vulkan.swapChain->getSwapChainImageFormat();
  if (format != vk::Format::eR8G8B8A8Unorm &&
      format != vk::Format::eB8G8R8A8Unorm)
    throw std::runtime_error(
        "Studio capture requires an RGBA8/BGRA8 swapchain");
  const size_t pixels = size_t(context.window.size.x) * context.window.size.y;
  for (size_t i = 0; i < pixels; ++i) {
    if (format == vk::Format::eB8G8R8A8Unorm)
      std::swap(bytes[i * 4], bytes[i * 4 + 2]);
    for (size_t c = 0; c < 3; ++c) {
      const float linear = bytes[i * 4 + c] / 255.f;
      const float srgb = linear <= .0031308f
                             ? 12.92f * linear
                             : 1.055f * std::pow(linear, 1.f / 2.4f) - .055f;
      bytes[i * 4 + c] = uint8_t(std::round(std::clamp(srgb, 0.f, 1.f) * 255));
    }
    bytes[i * 4 + 3] = 255;
  }
  const int ok =
      stbi_write_png(path, context.window.size.x, context.window.size.y, 4,
                     bytes, context.window.size.x * 4);
  readback.unmap();
  if (!ok)
    throw std::runtime_error("Could not write wolf studio capture");
  std::println("[wolf studio] saved {}", path);
}
} // namespace

void runWolfStudio() {
  vkh::EngineContext context{};
  context.worldConfig = vkh::world::loadConfig("config/world.toml");
  context.window.size = {1280, 960};
  vkh::initWindow(context);
  glfwSetWindowTitle(
      context.window,
      "Wolf studio — 1: three-quarter  2: side  3: front  F: fur  Esc: close");
  vkh::init(context);
  vkh::renderer::init(context);
  {
    vk::DescriptorBindingFlags flags =
        vk::DescriptorBindingFlagBits::ePartiallyBound;
    vk::DescriptorSetLayoutBindingFlagsCreateInfo flagInfo{};
    flagInfo.bindingCount = 1;
    flagInfo.pBindingFlags = &flags;
    auto textures = vkh::buildDescriptorSetLayout(
        context,
        {{0, vk::DescriptorType::eCombinedImageSampler, 256,
          vk::ShaderStageFlagBits::eFragment}},
        {}, &flagInfo);
    {
      vkh::WolfSys wolf(context, textures);
      vkh::PostProcessingSys display(
          context,
          {vkh::makeComputePassFactory<glm::vec4>([](vkh::EngineContext &) {
            return std::make_unique<StudioDisplay>();
          })});
      context.environment.sunDirection = {-.55f, .8f, .65f};
      context.environment.sunColor = {.98f, .96f, .92f};
      context.environment.sunIntensity = 2.0f;
      context.environment.skyAmbient = {.68f, .72f, .78f};
      context.environment.groundAmbient = {.30f, .32f, .36f};
      context.environment.windSpeed = context.environment.windStrength = 0;
      context.environment.fogDensity = 0;
      context.time = 0;
      int view = 1;
      if (const char *value = std::getenv("VULKHAN_WOLF_STUDIO_VIEW"))
        view = std::clamp(std::atoi(value), 1, 3);
      const char *capture = std::getenv("VULKHAN_WOLF_STUDIO_CAPTURE");
      std::array<bool, 4> wasPressed{};
      uint32_t frame = 0;
      std::println(
          "[wolf studio] one stationary rest-pose wolf; 1/2/3 views, F fur");
      while (!glfwWindowShouldClose(context.window)) {
        glfwPollEvents();
        if (glfwGetKey(context.window, GLFW_KEY_ESCAPE) == GLFW_PRESS)
          break;
        constexpr std::array keys{GLFW_KEY_1, GLFW_KEY_2, GLFW_KEY_3,
                                  GLFW_KEY_F};
        for (size_t i = 0; i < keys.size(); ++i) {
          bool pressed = glfwGetKey(context.window, keys[i]) == GLFW_PRESS;
          if (pressed && !wasPressed[i]) {
            if (i < 3)
              view = int(i) + 1;
            else
              wolf.setFurVisible(!wolf.isFurVisible());
          }
          wasPressed[i] = pressed;
        }
        constexpr std::array positions{glm::vec3(3.3f, 1.95f, 4.0f),
                                       glm::vec3(4.7f, 1.5f, .15f),
                                       glm::vec3(0, 1.5f, 4.8f)};
        context.camera.position = positions[view - 1];
        constexpr glm::vec3 target(0, 1.f, .25f);
        context.camera.orientation = glm::quatLookAt(
            glm::normalize(target - context.camera.position),
            glm::vec3(0, 1, 0));
        context.camera.projectionMatrix = glm::perspective(
            glm::radians(42.f), context.window.aspectRatio, 100.f, .03f);
        context.camera.projectionMatrix[1][1] *= -1;
        vkh::camera::calcViewYXZ(context);
        auto cmd = vkh::renderer::beginFrame(context);
        if (!cmd)
          continue;
        const int slot = vkh::renderer::getFrameIndex();
        context.frameInfo = {slot, 0, cmd};
        vkh::GlobalUbo ubo{};
        ubo.proj = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.projView = ubo.proj * ubo.view;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.resolution = context.window.size;
        ubo.aspectRatio = context.window.aspectRatio;
        ubo.time = 0;
        vkh::applyEnvironment(ubo, context.environment);
        context.vulkan.globalUBOs[slot].write(&ubo);
        wolf.updateStudio();
        vkh::renderer::beginMsaaPass(context, cmd, {.035f, .05f, .075f, 1});
        wolf.render();
        vkh::renderer::endMsaaPass(cmd);
        wolf.rasterizeFur();
        vkh::renderer::beginOneXPass(context, cmd);
        wolf.renderFur();
        vkh::renderer::endOneXPass(cmd);
        display.run(vkh::renderer::getCurrentImageIndex());
        std::unique_ptr<vkh::Buffer<uint8_t>> readback;
        if (capture && frame == 3) {
          readback = std::make_unique<vkh::Buffer<uint8_t>>(
              context, vk::BufferUsageFlagBits::eTransferDst,
              vk::MemoryPropertyFlagBits::eHostVisible |
                  vk::MemoryPropertyFlagBits::eHostCoherent,
              uint32_t(context.window.size.x * context.window.size.y * 4));
          recordCapture(context, *readback);
        }
        vkh::renderer::endFrame(context);
        if (readback) {
          saveCapture(context, *readback, capture);
          break;
        }
        ++frame;
      }
      context.vulkan.device.waitIdle();
    }
    context.vulkan.device.destroyDescriptorSetLayout(textures);
  }
  vkh::renderer::cleanup(context);
  vkh::cleanup(context);
  vkh::cleanupWindow(context);
}
} // namespace app
