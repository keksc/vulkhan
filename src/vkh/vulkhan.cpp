#include "vulkhan.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <fmt/core.h>

#include "controller.hpp"
#include "buffer.hpp"
#include "camera.hpp"
#include "systems/simple_render_system.hpp"
#include "systems/point_light_system.hpp"
#include "descriptors.hpp"
#include "gameObject.hpp"
#include "renderer.hpp"
#include "engineContext.hpp"
#include "init.hpp"
#include "ecs.hpp"

#include <array>
#include <cassert>
#include <chrono>
#include <stdexcept>
#include <memory>
#include <vector>

namespace vkh {
	Entity makePointLight(EngineContext& context, float intensity = 10.f, float radius = 0.1f, glm::vec3 color = glm::vec3(1.f)) {
		auto entity = context.ecs.createEntity();
		context.ecs.addComponent(entity, Transform{ .scale{ radius, 1.f, 1.f } });
		context.ecs.addComponent(entity, PointLight{ .color = color, .lightIntensity = intensity });
		return entity;
	}
	void loadObjects(EngineContext& context) {
		std::shared_ptr<LveModel> lveModel = LveModel::createModelFromFile(context, "models/flat_vase.obj");
		auto flatVase = context.ecs.createEntity();
		context.ecs.addComponent(flatVase, Transform{ .translation{ -.5f, .5f, 0.f }, .scale{ 3.f, 1.5f, 3.f } });
		context.ecs.addComponent(flatVase, lveModel);

		lveModel = LveModel::createModelFromFile(context, "models/smooth_vase.obj");
		auto smoothVase = context.ecs.createEntity();
		context.ecs.addComponent(smoothVase, Transform{ .translation{ .5f, .5f, 0.f }, .scale{ 3.f, 1.5f, 3.f } });
		context.ecs.addComponent(smoothVase, lveModel);

		lveModel = LveModel::createModelFromFile(context, "models/quad.obj");
		auto floor = context.ecs.createEntity();
		context.ecs.addComponent(floor, Transform{ .translation{ 0.f, .5f, 0.f }, .scale{ 3.f, 1.5f, 3.f } });
		context.ecs.addComponent(floor, lveModel);

		std::vector<glm::vec3> lightColors{
			{1.f, .1f, .1f},
			{.1f, .1f, 1.f},
			{.1f, 1.f, .1f},
			{1.f, 1.f, .1f},
			{.1f, 1.f, 1.f},
			{1.f, 1.f, 1.f}
		};

		for (int i = 0; i < lightColors.size(); i++) {
			auto pointLight = makePointLight(context, 0.2f, 0.1f, lightColors[i]);
			auto rotateLight = glm::rotate(
				glm::mat4(1.f),
				(i * glm::two_pi<float>()) / lightColors.size(),
				{ 0.f, -1.f, 0.f });
			auto& transform = context.ecs.getComponent<Transform>(pointLight);
			transform.translation = glm::vec3(rotateLight * glm::vec4(-1.f, -1.f, -1.f, 1.f));
		}
		lveModel = LveModel::createModelFromFile(context, "models/cube.obj");
		for (int i = 0; i < 30; i++) {
			for (int j = 0; j < 30; j++) {
				auto cube = context.ecs.createEntity();
				context.ecs.addComponent(cube, Transform{ .translation{  2.f + static_cast<float>(i) * 2.f, .5f, 2.f + static_cast<float>(j) * 2.f }, .scale{ .5f, .5f, .5f } });
				context.ecs.addComponent(cube, lveModel);
			}
		}
	}
	void framebufferResizeCallback(GLFWwindow* window, int width, int height) {
		auto context = reinterpret_cast<EngineContext*>(glfwGetWindowUserPointer(window));
		context->window.framebufferResized = true;
		context->window.width = width;
		context->window.height = height;
	}
	void initWindow(EngineContext& context) {
		glfwInit();
		glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
		glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
		context.window.glfwWindow = glfwCreateWindow(context.window.width, context.window.height, "Vulkhan", nullptr, nullptr);
		glfwSetWindowUserPointer(context.window, &context);
		glfwSetFramebufferSizeCallback(context.window, framebufferResizeCallback);
	}
	void cleanupWindow(EngineContext& context) {
		glfwDestroyWindow(context.window);
		glfwTerminate();
	}
	void initVulkan(EngineContext& context) {
		createInstance(context);
		setupDebugMessenger(context);
		glfwCreateWindowSurface(context.vulkan.instance, context.window, nullptr, &context.vulkan.surface);
		pickPhysicalDevice(context);
		createLogicalDevice(context);
		createCommandPool(context);
	}
	void cleanupVulkan(EngineContext& context) {
		vkDestroyCommandPool(context.vulkan.device, context.vulkan.commandPool, nullptr);
		vkDestroyDevice(context.vulkan.device, nullptr);

		if (context.vulkan.enableValidationLayers) {
			reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(vkGetInstanceProcAddr(context.vulkan.instance, "vkDestroyDebugUtilsMessengerEXT"))(context.vulkan.instance, context.vulkan.debugMessenger, nullptr);
		}

		vkDestroySurfaceKHR(context.vulkan.instance, context.vulkan.surface, nullptr);
		vkDestroyInstance(context.vulkan.instance, nullptr);
	}
	void run() {
		EngineContext context{};
		initWindow(context);
		initVulkan(context);
		renderer::init(context);

		std::unique_ptr<LveDescriptorPool> globalPool{};

		globalPool = LveDescriptorPool::Builder(context)
			.setMaxSets(swapChain::MAX_FRAMES_IN_FLIGHT)
			.addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, swapChain::MAX_FRAMES_IN_FLIGHT)
			.build();

		std::vector<std::unique_ptr<LveBuffer>> uboBuffers(swapChain::MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < uboBuffers.size(); i++) {
			uboBuffers[i] = std::make_unique<LveBuffer>(
				context,
				sizeof(GlobalUbo),
				1,
				VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
				VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
			uboBuffers[i]->map();
		}

		auto globalSetLayout =
			LveDescriptorSetLayout::Builder(context)
			.addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
			.build();

		context.ecs.Init();
		context.ecs.registerComponent<Transform>();
		context.ecs.registerComponent<PointLight>();
		context.ecs.registerComponent<std::shared_ptr<LveModel>>();

		auto simpleRenderSystem = context.ecs.registerSystem<SimpleRenderSystem>(context, globalSetLayout->getDescriptorSetLayout());
		auto pointLightSystem = context.ecs.registerSystem<PointLightSystem>(context, globalSetLayout->getDescriptorSetLayout());
		auto cameraController = context.ecs.registerSystem<Controller>(context);

		{
			Signature signature;
			signature.set(context.ecs.getComponentType<Transform>());
			signature.set(context.ecs.getComponentType<std::shared_ptr<LveModel>>());
			context.ecs.setSystemSignature<SimpleRenderSystem>(signature);
		}
		{
			Signature signature;
			signature.set(context.ecs.getComponentType<Transform>());
			signature.set(context.ecs.getComponentType<PointLight>());
			context.ecs.setSystemSignature<PointLightSystem>(signature);
		}
		{
			Signature signature;
			signature.set(context.ecs.getComponentType<Transform>());
			signature.set(context.ecs.getComponentType<std::shared_ptr<LveModel>>());
			context.ecs.setSystemSignature<Controller>(signature);
		}

		loadObjects(context);

		std::vector<VkDescriptorSet> globalDescriptorSets(swapChain::MAX_FRAMES_IN_FLIGHT);
		for (int i = 0; i < globalDescriptorSets.size(); i++) {
			auto bufferInfo = uboBuffers[i]->descriptorInfo();
			LveDescriptorWriter(*globalSetLayout, *globalPool)
				.writeBuffer(0, &bufferInfo)
				.build(globalDescriptorSets[i]);
		}

		LveCamera camera{};

		auto viewerEntity = context.ecs.createEntity();
		context.ecs.addComponent(viewerEntity, Transform{ .translation{ 0.f, 0.f, -2.5f } });

		auto currentTime = std::chrono::high_resolution_clock::now();
		while (!glfwWindowShouldClose(context.window)) {
			glfwPollEvents();

			auto newTime = std::chrono::high_resolution_clock::now();
			float frameTime = std::chrono::duration<float, std::chrono::seconds::period>(newTime - currentTime).count();
			fmt::print("FPS: {}\n", static_cast<int>(1.f / frameTime));
			currentTime = newTime;

			cameraController->moveInPlaneXZ(context, frameTime, viewerEntity);
			camera.setViewYXZ(context.ecs.getComponent<Transform>(viewerEntity).translation, context.ecs.getComponent<Transform>(viewerEntity).rotation);

			float aspect = renderer::getAspectRatio(context);
			camera.setPerspectiveProjection(glm::radians(50.f), aspect, 0.1f, 100.f);

			if (auto commandBuffer = renderer::beginFrame(context)) {
				int frameIndex = renderer::getFrameIndex();
				FrameInfo frameInfo{ frameIndex,
					frameTime,
					commandBuffer,
					camera,
					globalDescriptorSets[frameIndex],
				};

				// update
				GlobalUbo ubo{};
				ubo.projection = camera.getProjection();
				ubo.view = camera.getView();
				ubo.inverseView = camera.getInverseView();
				pointLightSystem->update(frameInfo, ubo);
				uboBuffers[frameIndex]->writeToBuffer(&ubo);
				uboBuffers[frameIndex]->flush();

				// render
				renderer::beginSwapChainRenderPass(context, commandBuffer);

				// order here matters
				simpleRenderSystem->renderGameObjects(frameInfo);
				pointLightSystem->render(frameInfo);
				renderer::endSwapChainRenderPass(commandBuffer);
				renderer::endFrame(context);
			}
		}

		vkDeviceWaitIdle(context.vulkan.device);

		renderer::cleanup(context);
		cleanupWindow(context);
	}
}
