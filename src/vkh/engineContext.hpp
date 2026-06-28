#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "window.hpp"

#include <filesystem>
#include <functional>
#include <memory>
#include <unordered_map>
#include <vector>

namespace vkh {

const std::filesystem::path CACHE_DIR = "cache";

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
};

const int NUM_BUFFERS = 2;

class SwapChain;

struct EngineContext {
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
    std::unique_ptr<SwapChain> swapChain;
    uint32_t maxFramesInFlight;
    std::unique_ptr<DescriptorAllocatorGrowable> globalDescriptorAllocator{};
    vk::DescriptorSetLayout globalDescriptorSetLayout;
    std::vector<Buffer<GlobalUbo>> globalUBOs;
    std::vector<vk::DescriptorSet> globalDescriptorSets;
    vk::Sampler defaultSampler;
    vk::SampleCountFlagBits msaaSamples;
    vk::PipelineCache pipelineCache;
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
