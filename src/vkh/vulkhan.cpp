#include "vulkhan.hpp"
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
#define STB_IMAGE_IMPLEMENTATION
#include <stb/stb_image.h>

#include "audio.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "descriptors.hpp"
#include "deviceHelpers.hpp"
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
#include "systems/water.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <memory>
#include <random>
#include <stdexcept>
#include <vector>

namespace vkh {
void loadObjects(EngineContext &context) {
  /*std::shared_ptr<Model> lveModel;
  lveModel =
      Model::createModelFromFile(context, "flat vase", "models/flat_vase.obj");
  context.entities.push_back({.transform = {.position{-.5f, GROUND_LEVEL, 0.f},
                                            .scale{3.f, 1.5f, 3.f}},
                              .model = lveModel});

  lveModel = Model::createModelFromFile(context, "smooth vase",
                                        "models/smooth_vase.obj");
  context.entities.push_back(
      {.transform = {.position{.5f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});*/

  /*lveModel = Model::createModelFromFile(context, "floor", "models/quad.obj");
  context.entities.push_back(
      {.transform = {.position{0.f, GROUND_LEVEL, 0.f}, .scale{3.f, 1.5f, 3.f}},
       .model = lveModel});*/

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
  lveModel = Model::createModelFromFile(context, "cube", "models/cube.obj");
  context.entities.push_back(
      {.transform = {.position = {5.f, -.5f, 0.f}, .scale = {.5f, .5f, .5f}},
       .model = lveModel});*/
  /*for (int i = 0; i < 30; i++) {
    std::random_device rd;  // Seed generator
    std::mt19937 gen(rd()); // Mersenne Twister engine
    std::uniform_int_distribution<> dist(0.f, 9.f);
    context.entities.push_back(
        {.transform = {.position = {2.f + dist(gen), -1.f, 2.f + dist(gen)},
                       .scale = {.5f, .5f, .5f}},
         .model = lveModel});
  }*/
  /*lveModel =
      Model::createModelFromFile(context, "living room", "models/mainRoom.obj");
  context.entities.push_back(
      {.transform = {.position{0.f, GROUND_LEVEL, 1.f}}, .model = lveModel});*/
  Model::Builder builder;
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
       .model = model});
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
VkImage textureImage;
VkDeviceMemory textureImageMemory;
VkImageView textureImageView;
VkSampler textureSampler;

void createTextureImage(EngineContext &context) {
  int texWidth, texHeight, texChannels;
  stbi_uc *pixels = stbi_load("textures/texture.jpg", &texWidth, &texHeight,
                              &texChannels, STBI_rgb_alpha);
  VkDeviceSize imageSize = texWidth * texHeight * 4;

  if (!pixels) {
    throw std::runtime_error("failed to load texture image!");
  }

  VkBuffer stagingBuffer;
  VkDeviceMemory stagingBufferMemory;
  createBuffer(context, imageSize, VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
               VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                   VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
               stagingBuffer, stagingBufferMemory);

  void *data;
  vkMapMemory(context.vulkan.device, stagingBufferMemory, 0, imageSize, 0,
              &data);
  memcpy(data, pixels, static_cast<size_t>(imageSize));
  vkUnmapMemory(context.vulkan.device, stagingBufferMemory);

  stbi_image_free(pixels);

  VkImageCreateInfo imageInfo{.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
                              .imageType = VK_IMAGE_TYPE_2D,
                              .format = VK_FORMAT_R8G8B8A8_SRGB,
                              .mipLevels = 1,
                              .arrayLayers = 1,
                              .samples = VK_SAMPLE_COUNT_1_BIT,
                              .tiling = VK_IMAGE_TILING_OPTIMAL,
                              .usage = VK_IMAGE_USAGE_TRANSFER_DST_BIT |
                                       VK_IMAGE_USAGE_SAMPLED_BIT,
                              .sharingMode = VK_SHARING_MODE_EXCLUSIVE,
                              .initialLayout = VK_IMAGE_LAYOUT_UNDEFINED};
  imageInfo.extent.width = texWidth;
  imageInfo.extent.height = texHeight;
  imageInfo.extent.depth = 1;
  createImageWithInfo(context, imageInfo, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                      textureImage, textureImageMemory);

  transitionImageLayout(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_UNDEFINED,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
  copyBufferToImage(context, stagingBuffer, textureImage,
                    static_cast<uint32_t>(texWidth),
                    static_cast<uint32_t>(texHeight));
  transitionImageLayout(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB,
                        VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  vkDestroyBuffer(context.vulkan.device, stagingBuffer, nullptr);
  vkFreeMemory(context.vulkan.device, stagingBufferMemory, nullptr);
}
void createTextureImageView(EngineContext &context) {
  textureImageView =
      createImageView(context, textureImage, VK_FORMAT_R8G8B8A8_SRGB);
}
void createTextureSampler(EngineContext &context) {
  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(context.vulkan.physicalDevice, &properties);

  VkSamplerCreateInfo samplerInfo{};
  samplerInfo.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
  samplerInfo.magFilter = VK_FILTER_LINEAR;
  samplerInfo.minFilter = VK_FILTER_LINEAR;
  samplerInfo.addressModeU = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeV = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.addressModeW = VK_SAMPLER_ADDRESS_MODE_REPEAT;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = properties.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = VK_COMPARE_OP_ALWAYS;
  samplerInfo.mipmapMode = VK_SAMPLER_MIPMAP_MODE_LINEAR;

  if (vkCreateSampler(context.vulkan.device, &samplerInfo, nullptr,
                      &textureSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }
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

    createTextureImage(context);
    createTextureImageView(context);
    createTextureSampler(context);

    globalPool = DescriptorPool::Builder(context)
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

    context.entities.clear();
    context.pointLights.clear();

    vkDestroySampler(context.vulkan.device, textureSampler, nullptr);
    vkDestroyImageView(context.vulkan.device, textureImageView, nullptr);
    vkDestroyImage(context.vulkan.device, textureImage, nullptr);
    vkFreeMemory(context.vulkan.device, textureImageMemory, nullptr);
  }

  renderer::cleanup(context);
  cleanupVulkan(context);
  cleanupWindow(context);
  cleanupAudio();
}
} // namespace vkh
