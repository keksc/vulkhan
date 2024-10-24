#include "vulkhan.hpp"
#include <cstdio>

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/core.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>

#include "buffer.hpp"
#include "camera.hpp"
#include "controller.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "gameObject.hpp"
#include "init.hpp"
#include "renderer.hpp"
#include "systems/pointLightSys.hpp"
#include "systems/objRenderSys.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

namespace vkh {
void loadObjects(EngineContext &context) {
  std::shared_ptr<LveModel> lveModel;
  lveModel = LveModel::createModelFromFile(context, "flat vase",
                                           "models/flat_vase.obj");
  context.entities.push_back(
      {.transform = {.translation{-.5f, .5f, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});

  lveModel = LveModel::createModelFromFile(context, "smooth vase",
                                           "models/smooth_vase.obj");
  context.entities.push_back(
      {.transform = {.translation{.5f, .5f, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});

  lveModel = LveModel::createModelFromFile(context, "floor", "models/quad.obj");
  context.entities.push_back(
      {.transform = {.translation{0.f, .5f, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});

  std::vector<glm::vec3> lightColors{{1.f, .1f, .1f}, {.1f, .1f, 1.f},
                                     {.1f, 1.f, .1f}, {1.f, 1.f, .1f},
                                     {.1f, 1.f, 1.f}, {1.f, 1.f, 1.f}};

  for (int i = 0; i < lightColors.size(); i++) {
    auto rotateLight = glm::rotate(
        glm::mat4(1.f), (i * glm::two_pi<float>()) / lightColors.size(),
        {0.f, -1.f, 0.f});
    context.pointLights.push_back(
        {.color = lightColors[i],
         .lightIntensity = 0.2f,
         .transform = {.translation = glm::vec3(
                           rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f)),
                       .scale{0.1f, 1.f, 1.f}}});
  }
  lveModel = LveModel::createModelFromFile(context, "cube", "models/cube.obj");
  for (int i = 0; i < 30; i++) {
    for (int j = 0; j < 30; j++) {
      context.entities.push_back(
          {.transform = {.translation = {2.f + static_cast<float>(i) * 2.f,
                                         -1.f,
                                         2.f + static_cast<float>(j) * 2.f},
                         .scale = {.5f, .5f, .5f}},
           .model = lveModel});
    }
  }
}
void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->window.framebufferResized = true;
  context->window.width = width;
  context->window.height = height;
}
void initWindow(EngineContext &context) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  context.window.glfwWindow = glfwCreateWindow(
      context.window.width, context.window.height, "Vulkhan", nullptr, nullptr);
  glfwSetWindowUserPointer(context.window, &context);
  glfwSetFramebufferSizeCallback(context.window, framebufferResizeCallback);
}
void cleanupWindow(EngineContext &context) {
  glfwDestroyWindow(context.window);
  glfwTerminate();
}
void initVulkan(EngineContext &context) {
  createInstance(context);
  setupDebugMessenger(context);
  glfwCreateWindowSurface(context.vulkan.instance, context.window, nullptr,
                          &context.vulkan.surface);
  pickPhysicalDevice(context);
  createLogicalDevice(context);
  createCommandPool(context);
}
void cleanupVulkan(EngineContext &context) {
  vkDestroyCommandPool(context.vulkan.device, context.vulkan.commandPool,
                       nullptr);
  vkDestroyDevice(context.vulkan.device, nullptr);

  if (context.vulkan.enableValidationLayers) {
    reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(
        context.vulkan.instance, "vkDestroyDebugUtilsMessengerEXT"))(
        context.vulkan.instance, context.vulkan.debugMessenger, nullptr);
  }

  vkDestroySurfaceKHR(context.vulkan.instance, context.vulkan.surface, nullptr);
  vkDestroyInstance(context.vulkan.instance, nullptr);
}
void run() {
  EngineContext context{.camera = {{0.f, 0.f, -2.5f}}};
  initWindow(context);
  initVulkan(context);
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkah is cleaned up
    std::unique_ptr<LveDescriptorPool> globalPool{};

    globalPool = LveDescriptorPool::Builder(context)
                     .setMaxSets(swapChain::MAX_FRAMES_IN_FLIGHT)
                     .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                  swapChain::MAX_FRAMES_IN_FLIGHT)
                     .build();

    std::vector<std::unique_ptr<LveBuffer>> uboBuffers(
        swapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < uboBuffers.size(); i++) {
      uboBuffers[i] = std::make_unique<LveBuffer>(
          context, fmt::format("ubo #{}", i), sizeof(GlobalUbo), 1,
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
      uboBuffers[i]->map();
    }

    auto globalSetLayout = LveDescriptorSetLayout::Builder(context)
                               .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT)
                               .build();

    objRenderSys::init(
        context, globalSetLayout->getDescriptorSetLayout());
    pointLightSys::init(context, globalSetLayout->getDescriptorSetLayout());
    Controller cameraController(context);

    loadObjects(context);

    std::vector<VkDescriptorSet> globalDescriptorSets(
        swapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < globalDescriptorSets.size(); i++) {
      auto bufferInfo = uboBuffers[i]->descriptorInfo();
      LveDescriptorWriter(*globalSetLayout, *globalPool)
          .writeBuffer(0, &bufferInfo)
          .build(globalDescriptorSets[i]);
    }

    auto currentTime = std::chrono::high_resolution_clock::now();
    while (!glfwWindowShouldClose(context.window)) {
      glfwPollEvents();

      auto newTime = std::chrono::high_resolution_clock::now();
      float frameTime =
          std::chrono::duration<float, std::chrono::seconds::period>(
              newTime - currentTime)
              .count();
      // fmt::print("FPS: {}\n", static_cast<int>(1.f / frameTime));
      currentTime = newTime;

      cameraController.moveInPlaneXZ(context, frameTime);
      context.camera.calcViewYXZ();

      float aspect = renderer::getAspectRatio(context);
      context.camera.calcPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.f);

      if (auto commandBuffer = renderer::beginFrame(context)) {
        int frameIndex = renderer::getFrameIndex();
        context.frameInfo = {
            frameIndex,
            frameTime,
            commandBuffer,
            globalDescriptorSets[frameIndex],
        };

        // update
        GlobalUbo ubo{};
        ubo.projection = context.camera.getProjection();
        ubo.view = context.camera.getView();
        ubo.inverseView = context.camera.getInverseView();
        pointLightSys::update(context, ubo);
        uboBuffers[frameIndex]->writeToBuffer(&ubo);
        uboBuffers[frameIndex]->flush();

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        objRenderSys::renderGameObjects(context);
        pointLightSys::render(context);
        renderer::endSwapChainRenderPass(commandBuffer);
        renderer::endFrame(context);
      }
    }

    vkDeviceWaitIdle(context.vulkan.device);
    objRenderSys::cleanup(context);
    pointLightSys::cleanup(context);
    context.entities.clear();
    context.pointLights.clear();
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
}
} // namespace vkh
