#include "vulkhan.hpp"
#include <optional>
#include <vulkan/vulkan_core.h>

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <AL/al.h>
#include <glm/gtx/quaternion.hpp>

#include "audio.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "cleanupVulkan.hpp"
#include "descriptors.hpp"
#include "deviceHelpers.hpp"
#include "engineContext.hpp"
#include "entity.hpp"
#include "initVulkan.hpp"
#include "input.hpp"
#include "renderer.hpp"
#include "systems/axes.hpp"
#include "systems/entities.hpp"
#include "systems/freezeAnimation.hpp"
#include "systems/particles.hpp"
#include "systems/physics.hpp"
#include "systems/pointLight.hpp"
#include "systems/water.hpp"
#include "window.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

namespace vkh {
void loadObjects(EngineContext &context) {
  context.entities.push_back(
      {context,
       {.position{-.5f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       "flat vase",
       "models/flat_vase.obj"});

  context.entities.push_back(
      {context,
       {.position{.5f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       "flat vase",
       "models/smooth_vase.obj"});

  /*model = Model::createModelFromFile(context, "floor", "models/quad.obj");
  context.entities.push_back(
      {.transform = {.position{0.f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = model});*/

  /*std::vector<glm::vec3> lightColors{{1.f, .1f, .1f}, {.1f, .1f, 1.f},
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
  model = Model::createModelFromFile(context, "cube", "models/cube.obj");
  context.entities.push_back(
      {.transform = {.position = {5.f, -.5f, 0.f}, .scale = {.5f, .5f, .5f}},
       .model = model});*/
  /*for (int i = 0; i < 30; i++) {
    std::random_device rd;  // Seed generator
    std::mt19937 gen(rd()); // Mersenne Twister engine
    std::uniform_int_distribution<> dist(0.f, 9.f);
    context.entities.push_back(
        {.transform = {.position = {2.f + dist(gen), -1.f, 2.f + dist(gen)},
                       .scale = {.5f, .5f, .5f}},
         .model = model});
  }*/
  context.entities.push_back({context,
                              {.position = {0.f, GROUND_LEVEL, 1.f}},
                              "living room",
                              "models/mainRoom.obj"});
  context.entities.push_back({context,
                              {.position = {0.f, GROUND_LEVEL - 1.f, 1.f},
                               .orientation = {0.0, {0.0, 0.0, 0.0}}},
                              "viking room",
                              "models/viking_room.obj",
                              "textures/viking_room.png"});
  /*Model::Builder builder;
  builder.vertices.push_back({{-0.5f, -0.5f, 0.f},
                              {1.0f, 0.0f, 0.0f},
                              {0.0f, 0.0f, 0.0f},
                              {1.0f, 0.0f}});
  builder.vertices.push_back({{0.5f, -0.5f, 0.f},
                              {0.0f, 1.0f, 0.0f},
                              {0.0f, 0.0f, 0.0f},
                              {0.0f, 0.0f}});
  builder.vertices.push_back({{0.5f, 0.5f, 0.f},
                              {0.0f, 0.0f, 1.0f},
                              {0.0f, 0.0f, 0.0f},
                              {0.0f, 1.0f}});
  builder.vertices.push_back({{-0.5f, -0.5f, 0.f},
                              {1.0f, 1.0f, 1.0f},
                              {0.0f, 0.0f, 0.0f},
                              {1.0f, 0.0f}});
  builder.vertices.push_back({{-0.5f, 0.5f, 0.f},
                              {1.0f, 1.0f, 1.0f},
                              {0.0f, 0.0f, 0.0f},
                              {1.0f, 1.0f}});
  builder.vertices.push_back({{0.5f, 0.5f, 0.f},
                              {1.0f, 1.0f, 1.0f},
                              {0.0f, 0.0f, 0.0f},
                              {0.0f, 1.0f}});
  std::shared_ptr<Model> model =
      std::make_shared<Model>(context, "img", builder);
  context.entities.push_back(
      {.transform = {.position = {0.f, GROUND_LEVEL - 2.f, 1.f}},
       .model = model});*/
}
VkImage textureImage;
VkDeviceMemory textureImageMemory;
VkImageView textureImageView;
VkSampler textureSampler;

void run() {
  EngineContext context{};
  context.entities.push_back(
      {context, {.position = {0.f, GROUND_LEVEL, 0.f}}, "player"});
  initWindow(context);
  initVulkan(context);
  initAudio();
  renderer::init(context);
  { // {} to handle call destructors of buffers before vulkah is cleaned up
    input::init(context);

    textureImage = createTextureImage(context, textureImageMemory,
                                      "textures/viking_room.png");
    textureImageView =
        createImageView(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB);
    textureSampler = createTextureSampler(context);

    std::unique_ptr<DescriptorPool> globalPool =
        DescriptorPool::Builder(context)
            .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT)
            .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                         SwapChain::MAX_FRAMES_IN_FLIGHT)
            .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
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

    auto globalSetLayout =
        DescriptorSetLayout::Builder(context)
            .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                        VK_SHADER_STAGE_VERTEX_BIT |
                            VK_SHADER_STAGE_FRAGMENT_BIT)
            .addBinding(1, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                        VK_SHADER_STAGE_FRAGMENT_BIT)
            .build();

    entitySys::init(context, globalSetLayout->getDescriptorSetLayout());
    pointLightSys::init(context, globalSetLayout->getDescriptorSetLayout());
    axesSys::init(context, globalSetLayout->getDescriptorSetLayout());
    particlesSys::init(context, globalSetLayout->getDescriptorSetLayout());
    freezeAnimationSys::init(context,
                             globalSetLayout->getDescriptorSetLayout());
    waterSys::init(context, globalSetLayout->getDescriptorSetLayout());

    loadObjects(context);

    std::vector<VkDescriptorSet> globalDescriptorSets(
        SwapChain::MAX_FRAMES_IN_FLIGHT);
    for (int i = 0; i < globalDescriptorSets.size(); i++) {
      VkDescriptorImageInfo imageInfo{};
      imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
      imageInfo.imageView = textureImageView;
      imageInfo.sampler = textureSampler;
      auto bufferInfo = uboBuffers[i]->descriptorInfo();
      DescriptorWriter(*globalSetLayout, *globalPool)
          .writeBuffer(0, &bufferInfo)
          .writeImage(1, &imageInfo)
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
      glm::vec3 forward = glm::rotate(
          transform.orientation, glm::vec3(0.0f, 0.0f, 1.0f)); // Forward vector

      glm::vec3 up = glm::rotate(transform.orientation,
                                 glm::vec3(0.0f, -1.0f, 0.0f)); // Up vector

      alListener3f(AL_POSITION, transform.position.x, transform.position.y,
                   transform.position.z);
      float orientationArray[6] = {forward.x, forward.y, forward.z,
                                   up.x,      up.y,      up.z};
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
        waterSys::render(context);
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
    waterSys::cleanup(context);

    vkDestroySampler(context.vulkan.device, textureSampler, nullptr);
    vkDestroyImageView(context.vulkan.device, textureImageView, nullptr);
    vkDestroyImage(context.vulkan.device, textureImage, nullptr);
    vkFreeMemory(context.vulkan.device, textureImageMemory, nullptr);

    context.entities.clear();
    context.pointLights.clear();
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
  cleanupAudio();
}
} // namespace vkh
