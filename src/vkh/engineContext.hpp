#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
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
const int MAX_SAMPLERS = 1000;
const int MAX_STORAGE_IMAGES = 100;
const int MAX_STORAGE_BUFFERS = 100;
class DescriptorPool;
class DescriptorSetLayout;
template <typename T> class Buffer;

struct GlobalUbo {
  alignas(16) glm::mat4 projView{1.f};
  alignas(16) glm::mat4 inverseView{1.f};
  alignas(4) float aspectRatio;
};

const int NUM_BUFFERS = 2;

class SwapChain;
struct EngineContext {
  EngineContext &operator=(const EngineContext &) = delete;
  struct {
    glm::ivec2 size = {800, 600};
    bool framebufferResized = false;
    bool fullscreen = false;
    VkExtent2D getExtent() {
      return {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)};
    };
    float aspectRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
    operator GLFWwindow *() { return glfwWindow; }
    GLFWwindow *glfwWindow;
  } window;
  struct {
    VkInstance instance;
#ifdef NDEBUG
    const bool enableValidationLayers = false;
#else
    const bool enableValidationLayers = true;
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
    std::unique_ptr<DescriptorPool> globalDescriptorPool;
    std::unique_ptr<DescriptorSetLayout> globalDescriptorSetLayout;
    std::vector<std::unique_ptr<Buffer<GlobalUbo>>> globalUBOs;
    std::vector<VkDescriptorSet> globalDescriptorSets;
  } vulkan;
  struct {
    int frameIndex;
    float dt;
    VkCommandBuffer commandBuffer;
    VkDescriptorSet globalDescriptorSet;
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
    std::unordered_map<void *, std::function<void(int, int, int)>> mouseButton;
    std::unordered_map<void *, std::function<void(double, double)>>
        cursorPosition;
    std::unordered_map<void *, std::function<void(unsigned int)>> character;
    std::unordered_map<void *, std::function<void(int, int, int, int)>> key;
    std::unordered_map<void *, std::function<void()>> doubleClick;
  };
  std::unordered_map<void *, InputCallbackSystem> inputCallbackSystems;
  void *currentInputCallbackSystemKey;
  struct {
    glm::vec2 cursorPos;
  } input;
};
} // namespace vkh
