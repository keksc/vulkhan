#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <AL/al.h>
#include <AL/alc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

#include <functional>
#include <memory>

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
};

const int NUM_BUFFERS = 2;

class SwapChain;
struct EngineContext {
  EngineContext &operator=(const EngineContext &) = delete;
  struct {
    glm::ivec2 size = {800, 600};
    bool framebufferResized = false;
    VkExtent2D getExtent() {
      return {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)};
    };
    void setFullscreen(bool fullscreen) {
      if (fullscreen) {
        glfwSetWindowMonitor(*this, NULL, 100, 100, 800, 600, 0);
      } else {
        GLFWmonitor *monitor = glfwGetPrimaryMonitor();
        const GLFWvidmode *mode = glfwGetVideoMode(monitor);
        glfwSetWindowMonitor(*this, monitor, 0, 0, mode->width, mode->height,
                             mode->refreshRate);
      }
    }

    float aspectRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
    operator GLFWwindow *() { return glfwWindow; }
    GLFWwindow *glfwWindow;
    struct {
      GLFWcursor *arrow;
      GLFWcursor *ibeam;
    } cursors;
  } window;
  struct {
    VkInstance instance;
#ifndef NDEBUG
    struct {
      PFN_vkSetDebugUtilsObjectNameEXT setObjName;
      PFN_vkCmdBeginDebugUtilsLabelEXT beginLabel;
      PFN_vkCmdEndDebugUtilsLabelEXT endLabel;
    } debug;
#endif
    VkDebugUtilsMessengerEXT debugMessenger;

    VkSurfaceKHR surface;
    VkPhysicalDevice physicalDevice;
    VkPhysicalDeviceProperties physicalDeviceProperties;
    VkDevice device;
    VkQueue graphicsQueue;
    VkQueue computeQueue;
    VkQueue presentQueue;
    VkCommandPool commandPool;
    std::unique_ptr<SwapChain> swapChain;
    uint32_t maxFramesInFlight;
    std::unique_ptr<DescriptorAllocatorGrowable> globalDescriptorAllocator{};
    VkDescriptorSetLayout globalDescriptorSetLayout;
    std::vector<Buffer<GlobalUbo>> globalUBOs;
    std::vector<VkDescriptorSet> globalDescriptorSets;
    VkSampler defaultSampler;
  } vulkan;
  struct {
    int frameIndex;
    float dt;
    VkCommandBuffer cmd;
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
  };
  std::unordered_map<void *, InputCallbackSystem> inputCallbackSystems;
  void *currentInputCallbackSystemKey;
  struct {
    glm::vec2 cursorPos;
  } input;
  float time;
};
} // namespace vkh
