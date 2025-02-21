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

#include <memory>
#include <vector>

namespace vkh {
struct Entity;
const int MAX_SAMPLERS = 1000;
const int MAX_STORAGE_IMAGES = 10;
class DescriptorPool;
class DescriptorSetLayout;

struct Particle {
  alignas(16) glm::vec3 position{};
  alignas(16) glm::vec3 color{};
};

const int MAX_PARTICLES = 10;

struct GlobalUbo {
  alignas(16) glm::mat4 projection{1.f};
  alignas(16) glm::mat4 view{1.f};
  alignas(16) glm::mat4 inverseView{1.f};
  alignas(16) Particle particles[MAX_PARTICLES];
  alignas(4) int numParticles;
  alignas(4) float aspectRatio;
};

const float GROUND_LEVEL = .5f;

const int NUM_BUFFERS = 2;

class SwapChain;
struct EngineContext {
  struct {
    int width = 800;
    int height = 600;
    bool framebufferResized = false;
    bool fullscreen = false;
    VkExtent2D getExtent() {
      return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
    };
    float aspectRatio = static_cast<float>(width) / height;
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
    std::unique_ptr<DescriptorSetLayout> modelDescriptorSetLayout;
    std::unique_ptr<DescriptorSetLayout> globalDescriptorSetLayout;
    VkSampler fontSampler;
    VkSampler modelSampler;
  } vulkan;
  std::vector<Entity> entities;
  std::vector<Particle> particles;
  struct {
    int frameIndex;
    float dt;
    VkCommandBuffer commandBuffer;
    VkDescriptorSet globalDescriptorSet;
  } frameInfo;
  struct {
    glm::vec3 position{0.f, 0.f, -2.5f};
    glm::quat orientation;
    glm::mat4 projectionMatrix{1.f};
    glm::mat4 viewMatrix{1.f};
    glm::mat4 inverseViewMatrix{1.f};
  } camera;
};
} // namespace vkh
