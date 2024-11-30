#include "vulkhan.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <al.h>
#include <glm/gtx/quaternion.hpp>

#include "audio.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "descriptors.hpp"
#include "engineContext.hpp"
#include "entity.hpp"
#include "init.hpp"
#include "input.hpp"
#include "renderer.hpp"
#include "systems/axes.hpp"
#include "systems/entities.hpp"
#include "systems/freezeAnimation.hpp"
#include "systems/particles.hpp"
#include "systems/physics.hpp"
#include "systems/pointLight.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

namespace vkh {
void loadObjects(EngineContext &context) {
  std::shared_ptr<Model> lveModel;
  lveModel =
      Model::createModelFromFile(context, "flat vase", "models/flat_vase.obj");
  context.entities.push_back({.transform = {.position{-.5f, GROUND_LEVEL, 0.f},
                                            .scale{3.f, 1.5f, 3.f}},
                              .model = lveModel});

  lveModel = Model::createModelFromFile(context, "smooth vase",
                                        "models/smooth_vase.obj");
  context.entities.push_back(
      {.transform = {.position{.5f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});

  /*lveModel = Model::createModelFromFile(context, "floor", "models/quad.obj");
  context.entities.push_back(
      {.transform = {.position{0.f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});*/

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
         .transform = {.position = glm::vec3(rotateLight *
                                             glm::vec4(-1.f, -1.f, -1.f, 1.f)),
                       .scale{0.1f, 1.f, 1.f}}});
  }
  lveModel = Model::createModelFromFile(context, "cube", "models/cube.obj");
  context.entities.push_back(
      {.transform = {.position = {5.f, -.5f, 0.f}, .scale = {.5f, .5f, .5f}},
       .model = lveModel});
  /*for (int i = 0; i < 30; i++) {
    std::random_device rd;  // Seed generator
    std::mt19937 gen(rd()); // Mersenne Twister engine
    std::uniform_int_distribution<> dist(0.f, 9.f);
    context.entities.push_back(
        {.transform = {.position = {2.f + dist(gen), -1.f, 2.f + dist(gen)},
                       .scale = {.5f, .5f, .5f}},
         .model = lveModel});
  }*/
  lveModel = Model::createModelFromFile(context, "living room",
                                        "models/living-room.obj");
  context.entities.push_back(
      {.transform = {.position{0.f, GROUND_LEVEL, 1.f}}, .model = lveModel});
}
void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->window.framebufferResized = true;
  context->window.width = width;
  context->window.height = height;
  context->window.aspectRatio = static_cast<float>(width) / height;
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
  context.entities.push_back(
      {.transform = {.position = {0.f, GROUND_LEVEL, 0.f}}});
  initWindow(context);
  initVulkan(context);
  initAudio();
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkah is cleaned up
    input::init(context);

    std::unique_ptr<DescriptorPool> globalPool{};

    globalPool = DescriptorPool::Builder(context)
                     .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
                     .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                  SwapChain::MAX_FRAMES_IN_FLIGHT)
                     .build();

    std::vector<std::unique_ptr<Buffer>> uboBuffers(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < uboBuffers.size(); i++) {
      uboBuffers[i] = std::make_unique<Buffer>(
          context, fmt::format("ubo #{}", i), sizeof(GlobalUbo), 1,
          VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
          VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
      uboBuffers[i]->map();
    }

    auto globalSetLayout = DescriptorSetLayout::Builder(context)
                               .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                                           VK_SHADER_STAGE_VERTEX_BIT |
                                               VK_SHADER_STAGE_FRAGMENT_BIT)
                               .build();

    entitySys::init(context, globalSetLayout->getDescriptorSetLayout());
    pointLightSys::init(context, globalSetLayout->getDescriptorSetLayout());
    axesSys::init(context, globalSetLayout->getDescriptorSetLayout());
    particlesSys::init(context, globalSetLayout->getDescriptorSetLayout());
    freezeAnimationSys::init(context,
                             globalSetLayout->getDescriptorSetLayout());

    loadObjects(context);

    std::vector<VkDescriptorSet> globalDescriptorSets(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < globalDescriptorSets.size(); i++) {
      auto bufferInfo = uboBuffers[i]->descriptorInfo();
      DescriptorWriter(*globalSetLayout, *globalPool)
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
      context.camera.position = context.entities[0].transform.position +
                                glm::vec3{0.f, camera::HEIGHT, 0.f};
      context.camera.orientation = context.entities[0].transform.orientation;
      camera::calcViewYXZ(context);

      const Transform &transform = context.entities[0].transform;
      glm::vec3 forward =
          glm::rotate(transform.orientation,
                      glm::vec3(0.0f, 0.0f, -1.0f)); // Forward vector

      glm::vec3 up = glm::rotate(transform.orientation,
                                 glm::vec3(0.0f, -1.0f, 0.0f)); // Up vector
      glm::vec3 at = glm::rotate(transform.orientation,
                                 glm::vec3(0.0f, 0.0f, -1.0f)); // Forward vector

      alListener3f(AL_POSITION, transform.position.x, transform.position.y,
                   transform.position.z);
      float orientationArray[6] = {at.x, at.y, at.z, up.x, up.y, up.z};
      alListenerfv(AL_ORIENTATION, orientationArray);
      updateAudio(glfwGetTime());

      float aspect = renderer::getAspectRatio(context);
      camera::calcPerspectiveProjection(context, glm::radians(50.f), aspect,
                                        0.1f, 100.f);

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
          physicsSys::update(context);
        pointLightSys::update(context, ubo);
        uboBuffers[frameIndex]->writeToBuffer(&ubo);
        uboBuffers[frameIndex]->flush();

        // render
        renderer::beginSwapChainRenderPass(context, commandBuffer);

        // order here matters
        entitySys::render(context);
        pointLightSys::render(context);
        axesSys::render(context);
        // freezeAnimationSys::render(context);
        particlesSys::render(context);
        renderer::endSwapChainRenderPass(commandBuffer);
        renderer::endFrame(context);
      }
    }

    vkDeviceWaitIdle(context.vulkan.device);

    entitySys::cleanup(context);
    pointLightSys::cleanup(context);
    axesSys::cleanup(context);
    freezeAnimationSys::cleanup(context);
    particlesSys::cleanup(context);

    context.entities.clear();
    context.pointLights.clear();
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
  cleanupAudio();
}
} // namespace vkh
