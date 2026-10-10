#pragma once

#include <glm/glm.hpp>
#include <vk_mem_alloc.h>
#include <vulkan/vulkan.hpp>

#include "../world/config.hpp"
#include "environment.hpp"
#include "window.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <set>
#include <unordered_map>
#include <vector>

namespace vkh {

class DescriptorAllocatorGrowable;
template <typename T> class Buffer;

struct GlobalUbo {
  alignas(16) glm::mat4 proj{1.f};
  alignas(16) glm::mat4 view{1.f};
  alignas(16) glm::mat4 projView{1.f};
  alignas(16) glm::mat4 inverseView{1.f};
  alignas(8) glm::vec2 resolution{};
  alignas(4) float aspectRatio;
  alignas(4) float time;
  alignas(16) glm::vec4 sunDirection;
  alignas(16) glm::vec4 sunColor;
  alignas(16) glm::vec4 wind;
  alignas(16) glm::vec4 fogParams;
  alignas(16) glm::vec4 fogColor;
  alignas(16) glm::vec4 skyAmbient;
  alignas(16) glm::vec4 groundAmbient;
  alignas(16) glm::vec4 lightGrid;
  alignas(16) std::array<glm::vec4, NightLights::count> lightPositions;
  alignas(16) std::array<glm::vec4, NightLights::count> lightColors;
  alignas(16) std::array<glm::vec4, NightLights::count> lightPaths;
};

inline void applyEnvironment(GlobalUbo &ubo, const Environment &e) {
  ubo.sunDirection = glm::vec4(safeSunDirection(e), 0.f);
  ubo.sunColor = glm::vec4(e.sunColor, std::max(e.sunIntensity, 0.f));
  ubo.wind = glm::vec4(safeWindDirection(e), std::max(e.windSpeed, 0.f),
                       glm::clamp(e.windStrength, 0.f, 1.f));
  ubo.fogParams = {std::max(e.fogDensity, 0.f),
                   std::max(e.fogHeightFalloff, 0.f), e.fogBaseHeight,
                   glm::clamp(e.fogAnisotropy, 0.f, 0.85f)};
  ubo.fogColor = glm::vec4(e.fogColor, 0.f);
  ubo.skyAmbient = glm::vec4(e.skyAmbient, 0.f);
  ubo.groundAmbient = glm::vec4(e.groundAmbient, 0.f);
  ubo.lightGrid = e.lights.grid;
  ubo.lightPositions = e.lights.positions;
  ubo.lightColors = e.lights.colors;
  ubo.lightPaths = e.lights.paths;
}

const int NUM_BUFFERS = 2;

class SwapChain;

struct EngineContext {
  Environment environment;
  std::shared_ptr<const world::Config> worldConfig;
  EngineContext &operator=(const EngineContext &) = delete;
  Window window;

  struct {
    vk::Instance instance;
#ifndef NDEBUG
    struct {
      PFN_vkSetDebugUtilsObjectNameEXT setObjName;
      PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel;
      PFN_vkCmdEndDebugUtilsLabelEXT endLabel;
    } debug;
#endif
    vk::DebugUtilsMessengerEXT debugMessenger;

    vk::SurfaceKHR surface;
    vk::PhysicalDevice physicalDevice;
    vk::PhysicalDeviceProperties physicalDeviceProperties;
    vk::Device device;
    vk::Queue graphicsQueue;
    vk::Queue computeQueue;
    vk::Queue presentQueue;
    vk::CommandPool commandPool;
    VmaAllocator allocator;
    std::unique_ptr<SwapChain> swapChain;
    uint32_t maxFramesInFlight;
    std::unique_ptr<DescriptorAllocatorGrowable> globalDescriptorAllocator{};
    vk::DescriptorSetLayout globalDescriptorSetLayout;
    std::vector<Buffer<GlobalUbo>> globalUBOs;
    std::unique_ptr<Buffer<world::GpuData>> worldUbo;
    std::vector<vk::DescriptorSet> globalDescriptorSets;
    vk::Sampler defaultSampler;
    vk::SampleCountFlagBits msaaSamples;
    vk::PipelineCache pipelineCache;
    bool strandFurAvailable = false;
    std::set<std::string> enabledOptionalExtensions;
    bool isRayTracingAvailable() {
      return enabledOptionalExtensions.contains(
                 VK_KHR_RAY_TRACING_PIPELINE_EXTENSION_NAME) &&
             enabledOptionalExtensions.contains(
                 VK_KHR_ACCELERATION_STRUCTURE_EXTENSION_NAME) &&
             enabledOptionalExtensions.contains(
                 VK_KHR_DEFERRED_HOST_OPERATIONS_EXTENSION_NAME);
    }
  } vulkan;

  struct {
    int frameIndex;
    float dt;
    vk::CommandBuffer cmd;
  } frameInfo;

  struct {
    float yaw = 0.f;
    float pitch = 0.f;
    glm::vec3 position{};
    glm::quat orientation{};
    glm::mat4 projectionMatrix{1.f};
    glm::mat4 viewMatrix{1.f};
    glm::mat4 inverseViewMatrix{1.f};
    float near = .01f;
    float far = 1000.f;
  } camera;

  struct InputCallbackSystem {
    std::function<void(int, int, int)> mouseButton;
    std::function<void(int, int, int, int)> key;
    std::function<void(double, double)> cursorPosition;
    std::function<void(unsigned int)> character;
    std::function<void(int, const char **)> drop;
    std::function<void(int)> windowFocus;
    std::function<void(double, double)> scroll;
  };

  std::unordered_map<void *, InputCallbackSystem> inputCallbackSystems;
  void *currentInputCallbackSystemKey;

  struct {
    glm::vec2 cursorPos;
  } input;

  float time;
};

} // namespace vkh
