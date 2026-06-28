#include "init.hpp"
#include "debug.hpp"

#include <GLFW/glfw3.h>
#include <magic_enum/magic_enum.hpp>
#include <vulkan/vulkan.hpp>

#include <print>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "deviceHelpers.hpp"
#include "pipeline.hpp"

namespace vkh {

const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_ROBUSTNESS_2_EXTENSION_NAME};

bool checkValidationLayerSupport() {
  uint32_t layerCount;
  if (vk::enumerateInstanceLayerProperties(&layerCount, nullptr) !=
      vk::Result::eSuccess) {
    return false;
  }

  std::vector<vk::LayerProperties> availableLayers(layerCount);
  if (vk::enumerateInstanceLayerProperties(
          &layerCount, availableLayers.data()) != vk::Result::eSuccess) {
    return false;
  }

  for (const char *layerName : validationLayers) {
    bool layerFound = false;

    for (const auto &layerProperties : availableLayers) {
      if (strcmp(layerName, layerProperties.layerName) == 0) {
        layerFound = true;
        break;
      }
    }

    if (!layerFound) {
      return false;
    }
  }

  return true;
}

static VKAPI_ATTR VkBool32 VKAPI_CALL
debugCallback(vk::DebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              vk::DebugUtilsMessageTypeFlagsEXT messageTypes,
              const vk::DebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {

  std::println("validation layers: {}", pCallbackData->pMessage);

  return VK_FALSE;
}

void populateDebugMessengerCreateInfo(
    vk::DebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = vk::DebugUtilsMessengerCreateInfoEXT{};
  createInfo.messageSeverity =
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eWarning |
      vk::DebugUtilsMessageSeverityFlagBitsEXT::eError;
  createInfo.messageType = vk::DebugUtilsMessageTypeFlagBitsEXT::eGeneral |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::eValidation |
                           vk::DebugUtilsMessageTypeFlagBitsEXT::ePerformance;
  createInfo.pfnUserCallback = debugCallback;
}

std::vector<const char *> getRequiredExtensions(EngineContext &context) {
  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

#ifndef NDEBUG
  extensions.emplace_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
#endif

  return extensions;
}

void checkGflwRequiredInstanceExtensions(EngineContext &context) {
  uint32_t extensionCount = 0;
  if (vk::enumerateInstanceExtensionProperties(
          nullptr, &extensionCount, nullptr) != vk::Result::eSuccess) {
    throw std::runtime_error("Failed to query extension properties count");
  }
  std::vector<vk::ExtensionProperties> extensions(extensionCount);
  if (vk::enumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                               extensions.data()) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("Failed to query extension properties data");
  }

  std::unordered_set<std::string> available;
  for (const auto &extension : extensions) {
    available.insert(extension.extensionName);
  }

  auto requiredExtensions = getRequiredExtensions(context);
  for (const auto &required : requiredExtensions) {
    if (available.find(required) == available.end()) {
      throw std::runtime_error("Missing required glfw extension");
    }
  }
}

void createInstance(EngineContext &context) {
#ifndef NDEBUG
  if (!checkValidationLayerSupport()) {
    throw std::runtime_error("validation layers requested, but not available!");
  }
#endif

  vk::ApplicationInfo appInfo{};
  appInfo.pApplicationName = "Vulkhan";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  vk::InstanceCreateInfo createInfo{};
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions(context);
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  vk::DebugUtilsMessengerCreateInfoEXT debugCreateInfo;
#ifndef NDEBUG
  createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
  createInfo.ppEnabledLayerNames = validationLayers.data();

  populateDebugMessengerCreateInfo(debugCreateInfo);
  createInfo.pNext = &debugCreateInfo;
#endif

  if (vk::createInstance(&createInfo, nullptr, &context.vulkan.instance) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create instance!");
  }

  checkGflwRequiredInstanceExtensions(context);
}

void setupDebugMessenger(EngineContext &context) {
  vk::DebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(createInfo);

  auto vkCreateDebugUtilsMessengerEXT =
      reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          context.vulkan.instance.getProcAddr(
              "vkCreateDebugUtilsMessengerEXT"));

  if (!vkCreateDebugUtilsMessengerEXT) {
    throw std::runtime_error(
        "failed to link vkCreateDebugUtilsMessengerEXT function!");
  }

  VkDebugUtilsMessengerEXT messenger;
  VkDebugUtilsMessengerCreateInfoEXT cStyleCreateInfo = createInfo;
  if (vkCreateDebugUtilsMessengerEXT(
          static_cast<VkInstance>(context.vulkan.instance), &cStyleCreateInfo,
          nullptr, &messenger) != VK_SUCCESS) {
    throw std::runtime_error("failed to set up debug messenger!");
  }
  context.vulkan.debugMessenger = messenger;
}

bool checkDeviceExtensionSupport(vk::PhysicalDevice device) {
  uint32_t extensionCount;
  if (device.enumerateDeviceExtensionProperties(
          nullptr, &extensionCount, nullptr) != vk::Result::eSuccess) {
    return false;
  }

  std::vector<vk::ExtensionProperties> availableExtensions(extensionCount);
  if (device.enumerateDeviceExtensionProperties(nullptr, &extensionCount,
                                                availableExtensions.data()) !=
      vk::Result::eSuccess) {
    return false;
  }

  std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                           deviceExtensions.end());

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}

unsigned int getDeviceScore(EngineContext &context, vk::PhysicalDevice device) {
  QueueFamilyIndices indices = findQueueFamilies(context, device);

  bool extensionsSupported = checkDeviceExtensionSupport(device);

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(context, device);
    swapChainAdequate = !swapChainSupport.formats.empty() &&
                        !swapChainSupport.presentModes.empty();
  }

  vk::PhysicalDeviceFeatures supportedFeatures = device.getFeatures();

  if (!(indices.isComplete() && extensionsSupported && swapChainAdequate &&
        supportedFeatures.samplerAnisotropy &&
        supportedFeatures.tessellationShader &&
        supportedFeatures.fillModeNonSolid))
    return 0; // unsuitable

  const auto &props = context.vulkan.physicalDeviceProperties;
  unsigned int score = 1;
  score += (props.deviceType == vk::PhysicalDeviceType::eDiscreteGpu);
  return score;
}

vk::SampleCountFlagBits getMaxUsableSampleCount(EngineContext &context) {
  auto &limits = context.vulkan.physicalDeviceProperties.limits;

  vk::SampleCountFlags counts =
      limits.framebufferColorSampleCounts & limits.framebufferDepthSampleCounts;

  if (counts & vk::SampleCountFlagBits::e64)
    return vk::SampleCountFlagBits::e64;
  if (counts & vk::SampleCountFlagBits::e32)
    return vk::SampleCountFlagBits::e32;
  if (counts & vk::SampleCountFlagBits::e16)
    return vk::SampleCountFlagBits::e16;
  if (counts & vk::SampleCountFlagBits::e8)
    return vk::SampleCountFlagBits::e8;
  if (counts & vk::SampleCountFlagBits::e4)
    return vk::SampleCountFlagBits::e4;
  if (counts & vk::SampleCountFlagBits::e2)
    return vk::SampleCountFlagBits::e2;

  return vk::SampleCountFlagBits::e1;
}

void pickPhysicalDevice(EngineContext &context) {
  uint32_t deviceCount = 0;
  if (context.vulkan.instance.enumeratePhysicalDevices(&deviceCount, nullptr) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
  }
  if (deviceCount == 0) {
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
  }
  std::vector<vk::PhysicalDevice> devices(deviceCount);
  if (context.vulkan.instance.enumeratePhysicalDevices(
          &deviceCount, devices.data()) != vk::Result::eSuccess) {
    throw std::runtime_error("failed to enumerate physical devices!");
  }

  unsigned int maxScore = 0;
  for (const auto &device : devices) {
    unsigned int score = getDeviceScore(context, device);
    if (!score) {
      continue;
    }
    if (score > maxScore) {
      maxScore = score;
      context.vulkan.physicalDevice = device;
    }
  }

  if (!context.vulkan.physicalDevice || !maxScore) {
    throw std::runtime_error("failed to find a suitable GPU!");
  }

  context.vulkan.physicalDeviceProperties =
      context.vulkan.physicalDevice.getProperties();

  vk::SampleCountFlagBits maxUsable = getMaxUsableSampleCount(context);
  context.vulkan.msaaSamples = vk::SampleCountFlagBits::e4;
  if (context.vulkan.msaaSamples > maxUsable) {
    context.vulkan.msaaSamples = maxUsable;
  }
}

void createLogicalDevice(EngineContext &context) {
  QueueFamilyIndices indices =
      findQueueFamilies(context, context.vulkan.physicalDevice);

  std::vector<vk::DeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {
      indices.graphicsFamily,
      indices.presentFamily,
  };

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    vk::DeviceQueueCreateInfo qInfo{};
    qInfo.queueFamilyIndex = queueFamily;
    qInfo.queueCount = 1;
    qInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(qInfo);
  }

  VkPhysicalDeviceRobustness2FeaturesKHR robustness2Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR,
      .nullDescriptor = VK_TRUE,
  };

  vk::PhysicalDeviceVulkan12Features vulkan12Features{};
  vulkan12Features.pNext = &robustness2Features;
  vulkan12Features.shaderSampledImageArrayNonUniformIndexing = VK_TRUE;
  vulkan12Features.descriptorBindingSampledImageUpdateAfterBind = VK_TRUE;
  vulkan12Features.descriptorBindingPartiallyBound = VK_TRUE;
  vulkan12Features.runtimeDescriptorArray = VK_TRUE;

  vk::PhysicalDeviceFeatures deviceFeatures{};
  deviceFeatures.tessellationShader = VK_TRUE;
  deviceFeatures.multiDrawIndirect = VK_TRUE;
  deviceFeatures.fillModeNonSolid = VK_TRUE;
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  vk::PhysicalDeviceFeatures2 deviceFeatures2{};
  deviceFeatures2.pNext = &vulkan12Features;
  deviceFeatures2.features = deviceFeatures;

  vk::DeviceCreateInfo createInfo{};
  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  createInfo.pNext = &deviceFeatures2;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  if (context.vulkan.physicalDevice.createDevice(&createInfo, nullptr,
                                                 &context.vulkan.device) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create logical device!");
  }

#ifndef NDEBUG
  context.vulkan.debug.setObjName =
      reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(
          context.vulkan.device.getProcAddr("vkSetDebugUtilsObjectNameEXT"));
  context.vulkan.debug.beginLabel =
      reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(
          context.vulkan.device.getProcAddr("vkCmdBeginDebugUtilsLabelEXT"));
  context.vulkan.debug.endLabel =
      reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(
          context.vulkan.device.getProcAddr("vkCmdEndDebugUtilsLabelEXT"));

  if (!context.vulkan.debug.setObjName || !context.vulkan.debug.beginLabel ||
      !context.vulkan.debug.endLabel) {
    throw std::runtime_error("Failed to load VK_EXT_debug_utils device "
                             "functions. Is the extension enabled?");
  }
#endif

  debug::setObjName(
      context, vk::ObjectType::eDevice,
      reinterpret_cast<uint64_t>(static_cast<VkDevice>(context.vulkan.device)),
      "logical device");

  context.vulkan.graphicsQueue =
      context.vulkan.device.getQueue(indices.graphicsFamily, 0);
  debug::setObjName(context, vk::ObjectType::eQueue,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkQueue>(context.vulkan.graphicsQueue)),
                    "graphics queue");

  context.vulkan.computeQueue =
      context.vulkan.device.getQueue(indices.computeFamily, 0);
  debug::setObjName(context, vk::ObjectType::eQueue,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkQueue>(context.vulkan.computeQueue)),
                    "compute queue");

  context.vulkan.presentQueue =
      context.vulkan.device.getQueue(indices.presentFamily, 0);
  debug::setObjName(context, vk::ObjectType::eQueue,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkQueue>(context.vulkan.presentQueue)),
                    "present queue");
}

void createCommandPool(EngineContext &context) {
  QueueFamilyIndices queueFamilyIndices =
      findQueueFamilies(context, context.vulkan.physicalDevice);

  vk::CommandPoolCreateInfo poolInfo{};
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
  poolInfo.flags = vk::CommandPoolCreateFlagBits::eTransient |
                   vk::CommandPoolCreateFlagBits::eResetCommandBuffer;

  if (context.vulkan.device.createCommandPool(&poolInfo, nullptr,
                                              &context.vulkan.commandPool) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create command pool!");
  }
}

std::string deviceTypeToString(vk::PhysicalDeviceType type) {
  switch (type) {
  case vk::PhysicalDeviceType::eIntegratedGpu:
    return "Integrated GPU";
  case vk::PhysicalDeviceType::eDiscreteGpu:
    return "Discrete GPU";
  case vk::PhysicalDeviceType::eVirtualGpu:
    return "Virtual GPU";
  case vk::PhysicalDeviceType::eCpu:
    return "CPU";
  default:
    return "Other";
  }
}

void displayInitInfo(EngineContext &context) {
  const auto &props = context.vulkan.physicalDeviceProperties;
  std::println("{:=<70}", "");

  std::println("{:<30} {:<25}", "Property", "Value");
  std::println("{:-<70}", "");

  std::println("{:<30} {:<25}", "Selected GPU",
               context.vulkan.physicalDeviceProperties.deviceName.data());
  std::println("{:<30} {}.{}.{}", "API Version",
               VK_VERSION_MAJOR(static_cast<uint32_t>(props.apiVersion)),
               VK_VERSION_MINOR(static_cast<uint32_t>(props.apiVersion)),
               VK_VERSION_PATCH(static_cast<uint32_t>(props.apiVersion)));

  std::println("{:<30} {:<25}", "Max frames in flight",
               context.vulkan.maxFramesInFlight);

  std::println("{:<30} {:<25}", "MSAA",
               magic_enum::enum_name(context.vulkan.msaaSamples));

  std::println("{:=<70}", "");
}

void setupGlobResources(EngineContext &context) {
  std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> poolSizeRatios = {
      {vk::DescriptorType::eStorageImage, 3},
      {vk::DescriptorType::eStorageBuffer, 3},
      {vk::DescriptorType::eUniformBuffer, 3},
      {vk::DescriptorType::eCombinedImageSampler, 4},
  };
  context.vulkan.globalDescriptorAllocator =
      std::make_unique<DescriptorAllocatorGrowable>(context);
  context.vulkan.globalDescriptorAllocator->init(100, poolSizeRatios);

  context.vulkan.globalUBOs.reserve(context.vulkan.maxFramesInFlight);
  for (int i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    auto &buf = context.vulkan.globalUBOs.emplace_back(
        context, vk::BufferUsageFlagBits::eUniformBuffer,
        vk::MemoryPropertyFlagBits::eHostVisible);
    buf.map();
  }

  vk::DescriptorSetLayoutBinding binding{};
  binding.binding = 0;
  binding.descriptorType = vk::DescriptorType::eUniformBuffer;
  binding.descriptorCount = 1;
  binding.stageFlags = vk::ShaderStageFlagBits::eAll;

  context.vulkan.globalDescriptorSetLayout =
      buildDescriptorSetLayout(context, {binding});

  context.vulkan.globalDescriptorSets.reserve(context.vulkan.maxFramesInFlight);
  for (int i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    auto &set = context.vulkan.globalDescriptorSets.emplace_back();
    set = context.vulkan.globalDescriptorAllocator->allocate(
        context.vulkan.globalDescriptorSetLayout);
    DescriptorWriter writer(context);
    writer.writeBuffer(0, context.vulkan.globalUBOs[i].descriptorInfo(),
                       vk::DescriptorType::eUniformBuffer);
    writer.updateSet(set);
  }
}

void init(EngineContext &context) {
  createInstance(context);

#ifndef NDEBUG
  setupDebugMessenger(context);
#endif

  VkSurfaceKHR rawSurface;
  if (glfwCreateWindowSurface(static_cast<VkInstance>(context.vulkan.instance),
                              context.window, nullptr,
                              &rawSurface) != ::VK_SUCCESS) {
    throw std::runtime_error("failed to create window surface!");
  }
  context.vulkan.surface = rawSurface;

  pickPhysicalDevice(context);
  createLogicalDevice(context);
  debug::setObjName(context, vk::ObjectType::eInstance,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkInstance>(context.vulkan.instance)),
                    "instance");

  auto swapChainSupport = getSwapChainSupport(context);
  uint32_t imageCount = (swapChainSupport.capabilities.minImageCount <= 2)
                            ? 2
                            : swapChainSupport.capabilities.minImageCount + 1;
  if (swapChainSupport.capabilities.maxImageCount != 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount)
    imageCount = swapChainSupport.capabilities.maxImageCount;

  context.vulkan.maxFramesInFlight = imageCount;

  createCommandPool(context);
  displayInitInfo(context);

  const auto &props = context.vulkan.physicalDeviceProperties;
  vk::SamplerCreateInfo samplerInfo{};
  samplerInfo.magFilter = vk::Filter::eLinear;
  samplerInfo.minFilter = vk::Filter::eLinear;
  samplerInfo.addressModeU = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeV = vk::SamplerAddressMode::eRepeat;
  samplerInfo.addressModeW = vk::SamplerAddressMode::eRepeat;
  samplerInfo.anisotropyEnable = VK_TRUE;
  samplerInfo.maxAnisotropy = props.limits.maxSamplerAnisotropy;
  samplerInfo.borderColor = vk::BorderColor::eIntOpaqueBlack;
  samplerInfo.unnormalizedCoordinates = VK_FALSE;
  samplerInfo.compareEnable = VK_FALSE;
  samplerInfo.compareOp = vk::CompareOp::eAlways;
  samplerInfo.mipmapMode = vk::SamplerMipmapMode::eLinear;

  if (context.vulkan.device.createSampler(&samplerInfo, nullptr,
                                          &context.vulkan.defaultSampler) !=
      vk::Result::eSuccess) {
    throw std::runtime_error("failed to create texture sampler!");
  }
  debug::setObjName(context, vk::ObjectType::eSampler,
                    reinterpret_cast<uint64_t>(
                        static_cast<VkSampler>(context.vulkan.defaultSampler)),
                    "default sampler");

  setupGlobResources(context);

  Pipeline::loadCache(context);
}

} // namespace vkh
