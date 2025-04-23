#include "initVulkan.hpp"

#include <GLFW/glfw3.h>
#include <fmt/core.h>
#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#include <set>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "deviceHelpers.hpp"
#include "swapChain.hpp"

namespace vkh {
const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME};

bool checkValidationLayerSupport() {
  uint32_t layerCount;
  vkEnumerateInstanceLayerProperties(&layerCount, nullptr);

  std::vector<VkLayerProperties> availableLayers(layerCount);
  vkEnumerateInstanceLayerProperties(&layerCount, availableLayers.data());

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
debugCallback(VkDebugUtilsMessageSeverityFlagBitsEXT messageSeverity,
              VkDebugUtilsMessageTypeFlagsEXT messageType,
              const VkDebugUtilsMessengerCallbackDataEXT *pCallbackData,
              void *pUserData) {
  fmt::print("validation layers: {}\n", pCallbackData->pMessage);

  return VK_FALSE;
}
void populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT;
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
  createInfo.pfnUserCallback = debugCallback;
  createInfo.pUserData = nullptr; // Optional
}
std::vector<const char *> getRequiredExtensions(EngineContext &context) {
  uint32_t glfwExtensionCount = 0;
  const char **glfwExtensions;
  glfwExtensions = glfwGetRequiredInstanceExtensions(&glfwExtensionCount);

  std::vector<const char *> extensions(glfwExtensions,
                                       glfwExtensions + glfwExtensionCount);

  if (context.vulkan.enableValidationLayers) {
    extensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
  }

  return extensions;
}
void checkGflwRequiredInstanceExtensions(EngineContext &context) {
  uint32_t extensionCount = 0;
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount, nullptr);
  std::vector<VkExtensionProperties> extensions(extensionCount);
  vkEnumerateInstanceExtensionProperties(nullptr, &extensionCount,
                                         extensions.data());

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
  if (context.vulkan.enableValidationLayers && !checkValidationLayerSupport()) {
    throw std::runtime_error("validation layers requested, but not available!");
  }

  VkApplicationInfo appInfo = {};
  appInfo.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
  appInfo.pApplicationName = "Vulkhan";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_0;

  VkInstanceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions(context);
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
  if (context.vulkan.enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();

    populateDebugMessengerCreateInfo(debugCreateInfo);
    createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
  }

  if (vkCreateInstance(&createInfo, nullptr, &context.vulkan.instance) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
  }

  checkGflwRequiredInstanceExtensions(context);
}
void setupDebugMessenger(EngineContext &context) {
  if (!context.vulkan.enableValidationLayers)
    return;
  VkDebugUtilsMessengerCreateInfoEXT createInfo;
  populateDebugMessengerCreateInfo(createInfo);
  if (reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(
          vkGetInstanceProcAddr(context.vulkan.instance,
                                "vkCreateDebugUtilsMessengerEXT"))(
          context.vulkan.instance, &createInfo, nullptr,
          &context.vulkan.debugMessenger) != VK_SUCCESS) {
    throw std::runtime_error("failed to set up debug messenger!");
  }
}
bool checkDeviceExtensionSupport(VkPhysicalDevice device) {
  uint32_t extensionCount;
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       nullptr);

  std::vector<VkExtensionProperties> availableExtensions(extensionCount);
  vkEnumerateDeviceExtensionProperties(device, nullptr, &extensionCount,
                                       availableExtensions.data());

  std::set<std::string> requiredExtensions(deviceExtensions.begin(),
                                           deviceExtensions.end());

  for (const auto &extension : availableExtensions) {
    requiredExtensions.erase(extension.extensionName);
  }

  return requiredExtensions.empty();
}
bool isDeviceSuitable(EngineContext &context, VkPhysicalDevice device) {
  QueueFamilyIndices indices = findQueueFamilies(context, device);

  bool extensionsSupported = checkDeviceExtensionSupport(device);

  bool swapChainAdequate = false;
  if (extensionsSupported) {
    SwapChainSupportDetails swapChainSupport =
        querySwapChainSupport(context, device);
    swapChainAdequate = !swapChainSupport.formats.empty() &&
                        !swapChainSupport.presentModes.empty();
  }

  VkPhysicalDeviceFeatures supportedFeatures;
  vkGetPhysicalDeviceFeatures(device, &supportedFeatures);

  return indices.isComplete() && extensionsSupported && swapChainAdequate &&
         supportedFeatures.samplerAnisotropy;
}
void pickPhysicalDevice(EngineContext &context) {
  uint32_t deviceCount = 0;
  vkEnumeratePhysicalDevices(context.vulkan.instance, &deviceCount, nullptr);
  if (deviceCount == 0) {
    throw std::runtime_error("failed to find GPUs with Vulkan support!");
  }
  std::vector<VkPhysicalDevice> devices(deviceCount);
  vkEnumeratePhysicalDevices(context.vulkan.instance, &deviceCount,
                             devices.data());

  for (const auto &device : devices) {
    if (isDeviceSuitable(context, device)) {
      context.vulkan.physicalDevice = device;
      break;
    }
  }

  if (context.vulkan.physicalDevice == VK_NULL_HANDLE) {
    throw std::runtime_error("failed to find a suitable GPU!");
  }

  vkGetPhysicalDeviceProperties(context.vulkan.physicalDevice,
                                &context.vulkan.physicalDeviceProperties);
}
void createLogicalDevice(EngineContext &context) {
  QueueFamilyIndices indices =
      findQueueFamilies(context, context.vulkan.physicalDevice);

  std::vector<VkDeviceQueueCreateInfo> queueCreateInfos;
  std::set<uint32_t> uniqueQueueFamilies = {indices.graphicsFamily,
                                            indices.presentFamily};

  float queuePriority = 1.0f;
  for (uint32_t queueFamily : uniqueQueueFamilies) {
    VkDeviceQueueCreateInfo queueCreateInfo = {};
    queueCreateInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
    queueCreateInfo.queueFamilyIndex = queueFamily;
    queueCreateInfo.queueCount = 1;
    queueCreateInfo.pQueuePriorities = &queuePriority;
    queueCreateInfos.push_back(queueCreateInfo);
  }

  VkPhysicalDeviceFeatures deviceFeatures = {};
  deviceFeatures.samplerAnisotropy = VK_TRUE;

  VkDeviceCreateInfo createInfo = {};
  createInfo.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;

  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  createInfo.pEnabledFeatures = &deviceFeatures;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  // might not really be necessary anymore because device specific validation
  // layers have been deprecated
  if (context.vulkan.enableValidationLayers) {
    createInfo.enabledLayerCount =
        static_cast<uint32_t>(validationLayers.size());
    createInfo.ppEnabledLayerNames = validationLayers.data();
  }

  if (vkCreateDevice(context.vulkan.physicalDevice, &createInfo, nullptr,
                     &context.vulkan.device) != VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
  }

  vkGetDeviceQueue(context.vulkan.device, indices.graphicsFamily, 0,
                   &context.vulkan.graphicsQueue);
  vkGetDeviceQueue(context.vulkan.device, indices.computeFamily, 0,
                   &context.vulkan.computeQueue);
  vkGetDeviceQueue(context.vulkan.device, indices.presentFamily, 0,
                   &context.vulkan.presentQueue);
}
void createCommandPool(EngineContext &context) {
  QueueFamilyIndices queueFamilyIndices =
      findQueueFamilies(context, context.vulkan.physicalDevice);

  VkCommandPoolCreateInfo poolInfo = {};
  poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
  poolInfo.queueFamilyIndex = queueFamilyIndices.graphicsFamily;
  poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
                   VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;

  if (vkCreateCommandPool(context.vulkan.device, &poolInfo, nullptr,
                          &context.vulkan.commandPool) != VK_SUCCESS) {
    throw std::runtime_error("failed to create command pool!");
  }
}
std::string deviceTypeToString(VkPhysicalDeviceType type) {
  switch (type) {
  case VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU:
    return "Integrated GPU";
  case VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU:
    return "Discrete GPU";
  case VK_PHYSICAL_DEVICE_TYPE_VIRTUAL_GPU:
    return "Virtual GPU";
  case VK_PHYSICAL_DEVICE_TYPE_CPU:
    return "CPU";
  default:
    return "Other";
  }
}
void displayInitInfo(EngineContext &context) {
  fmt::print("{:=<60}\n", ""); // Table border

  // Header row
  fmt::print("{:<30} {:<25}\n", "Property", "Value");
  fmt::print("{:-<60}\n", ""); // Divider

  // Device Info
  fmt::print("{:<30} {:<25}\n", "Selected GPU",
             context.vulkan.physicalDeviceProperties.deviceName);
  fmt::print(
      "{:<30} {}.{}.{}\n", "API Version",
      VK_VERSION_MAJOR(context.vulkan.physicalDeviceProperties.apiVersion),
      VK_VERSION_MINOR(context.vulkan.physicalDeviceProperties.apiVersion),
      VK_VERSION_PATCH(context.vulkan.physicalDeviceProperties.apiVersion));

  fmt::print("{:=<60}\n", ""); // Table border
}
void setupGlobResources(EngineContext &context) {
  context.vulkan.globalDescriptorPool =
      DescriptorPool::Builder(context)
          .setMaxSets(SwapChain::MAX_FRAMES_IN_FLIGHT + MAX_SAMPLERS)
          .addPoolSize(VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                       SwapChain::MAX_FRAMES_IN_FLIGHT + 90)
          .addPoolSize(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, MAX_SAMPLERS)
          .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, MAX_STORAGE_IMAGES)
          .addPoolSize(VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, MAX_STORAGE_BUFFERS)
          .build();

  context.vulkan.globalUBOs.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < context.vulkan.globalUBOs.size(); i++) {
    BufferCreateInfo bufInfo{};
    bufInfo.instanceSize = sizeof(GlobalUbo);
    bufInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
    bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT;
    context.vulkan.globalUBOs[i] = std::make_unique<Buffer>(context, bufInfo);
    context.vulkan.globalUBOs[i]->map();
  }

  context.vulkan.globalDescriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();

  context.vulkan.globalDescriptorSets.resize(SwapChain::MAX_FRAMES_IN_FLIGHT);
  for (int i = 0; i < context.vulkan.globalDescriptorSets.size(); i++) {
    auto bufferInfo = context.vulkan.globalUBOs[i]->descriptorInfo();
    DescriptorWriter(*context.vulkan.globalDescriptorSetLayout,
                     *context.vulkan.globalDescriptorPool)
        .writeBuffer(0, &bufferInfo)
        .build(context.vulkan.globalDescriptorSets[i]);
  }
}
void initVulkan(EngineContext &context) {
  createInstance(context);
  setupDebugMessenger(context);
  glfwCreateWindowSurface(context.vulkan.instance, context.window, nullptr,
                          &context.vulkan.surface);
  pickPhysicalDevice(context);
  createLogicalDevice(context);
  createCommandPool(context);
  displayInitInfo(context);
  setupGlobResources(context);
}
} // namespace vkh
