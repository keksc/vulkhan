#include "vulkhan.hpp"
#include <cstdio>
#include <fmt/base.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <fmt/format.h>

#include "buffer.hpp"
#include "camera.hpp"
#include "input.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "entity.hpp"
#include "init.hpp"
#include "renderer.hpp"
#include "systems/entitySys.hpp"
#include "systems/pointLightSys.hpp"
#include "systems/freezeAnimationSys.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <memory>
#include <stdexcept>
#include <vector>

namespace vkh {
void loadObjects(EngineContext &context) {
  std::shared_ptr<Model> lveModel;
  lveModel =
      Model::createModelFromFile(context, "flat vase", "models/flat_vase.obj");
  context.entities.push_back(
      {.transform = {.translation{-.5f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});

  lveModel = Model::createModelFromFile(context, "smooth vase",
                                        "models/smooth_vase.obj");
  context.entities.push_back(
      {.transform = {.translation{.5f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});

  lveModel = Model::createModelFromFile(context, "floor", "models/quad.obj");
  context.entities.push_back(
      {.transform = {.translation{0.f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
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
  lveModel = Model::createModelFromFile(context, "cube", "models/cube.obj");
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
  context->window.aspectRatio = static_cast<float>(width)/height;
}
void initWindow(EngineContext &context) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
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
  EngineContext context{};
  context.player.transform.translation = {0.f, GROUND_LEVEL, -2.5f};
  initWindow(context);
  initVulkan(context);
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkah is cleaned up
    input::init(context);

    std::unique_ptr<LveDescriptorPool> globalPool{};

    globalPool = LveDescriptorPool::Builder(context)
                     .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                     .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                  SwapChain::MAX_FRAMES_IN_FLIGHT)
                     .build();

    std::vector<std::unique_ptr<LveBuffer>> uboBuffers(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
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

    entitySys::init(context, globalSetLayout->getDescriptorSetLayout());
    pointLightSys::init(context, globalSetLayout->getDescriptorSetLayout());
    freezeAnimationSys::init(context, globalSetLayout->getDescriptorSetLayout());

    loadObjects(context);

    std::vector<VkDescriptorSet> globalDescriptorSets(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
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

      context.frameInfo.dt = frameTime;
      input::moveInPlaneXZ(context);
      context.camera.position = context.player.transform.translation + glm::vec3{0.f, camera::HEIGHT, 0.f};
      context.camera.rotation = context.player.transform.rotation;
      camera::calcViewYXZ(context);

      float aspect = renderer::getAspectRatio(context); // TODO: recreate the swapchain
      camera::calcPerspectiveProjection(context, glm::radians(50.f), aspect, 0.1f,
                                        100.f);

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
        ubo.projection = context.camera.projectionMatrix;
        ubo.view = context.camera.viewMatrix;
        ubo.inverseView = context.camera.inverseViewMatrix;
        ubo.aspectRatio = context.window.aspectRatio;
        if (glfwGetKey(context.window, GLFW_KEY_G))
          entitySys::update(context);
        pointLightSys::update(context, ubo);
        uboBuffers[frameIndex]->writeToBuffer(&ubo);
        uboBuffers[frameIndex]->flush();

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        entitySys::render(context);
        pointLightSys::render(context);
        freezeAnimationSys::render(context);
        renderer::endSwapChainRenderPass(commandBuffer);
        renderer::endFrame(context);
      }
    }

    vkDeviceWaitIdle(context.vulkan.device);
    entitySys::cleanup(context);
    pointLightSys::cleanup(context);
    freezeAnimationSys::cleanup(context);
    context.entities.clear();
    context.pointLights.clear();
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
}
} // namespace vkh
