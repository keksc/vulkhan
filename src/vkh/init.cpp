#include "init.hpp"
#include "debug.hpp"

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>

#include <print>
#include <set>
#include <stdexcept>
#include <unordered_set>
#include <vector>

#include "buffer.hpp"
#include "descriptors.hpp"
#include "deviceHelpers.hpp"

namespace vkh {
const std::vector<const char *> validationLayers = {
    "VK_LAYER_KHRONOS_validation"};
const std::vector<const char *> deviceExtensions = {
    VK_KHR_SWAPCHAIN_EXTENSION_NAME, VK_KHR_ROBUSTNESS_2_EXTENSION_NAME};

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
  std::println("validation layers: {}", pCallbackData->pMessage);

  return VK_FALSE;
}
void populateDebugMessengerCreateInfo(
    VkDebugUtilsMessengerCreateInfoEXT &createInfo) {
  createInfo = {.sType =
                    VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
  createInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT |
                               VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
  createInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_GENERAL_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT |
                           VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
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
#ifndef NDEBUG
  if (!checkValidationLayerSupport()) {
    throw std::runtime_error("validation layers requested, but not available!");
  }
#endif

  VkApplicationInfo appInfo = {.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO};
  appInfo.pApplicationName = "Vulkhan";
  appInfo.applicationVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.pEngineName = "No Engine";
  appInfo.engineVersion = VK_MAKE_VERSION(1, 0, 0);
  appInfo.apiVersion = VK_API_VERSION_1_3;

  VkInstanceCreateInfo createInfo = {
      .sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
  createInfo.pApplicationInfo = &appInfo;

  auto extensions = getRequiredExtensions(context);
  createInfo.enabledExtensionCount = static_cast<uint32_t>(extensions.size());
  createInfo.ppEnabledExtensionNames = extensions.data();

  VkDebugUtilsMessengerCreateInfoEXT debugCreateInfo;
#ifndef NDEBUG
  createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
  createInfo.ppEnabledLayerNames = validationLayers.data();

  populateDebugMessengerCreateInfo(debugCreateInfo);
  createInfo.pNext = (VkDebugUtilsMessengerCreateInfoEXT *)&debugCreateInfo;
#endif

  if (vkCreateInstance(&createInfo, nullptr, &context.vulkan.instance) !=
      VK_SUCCESS) {
    throw std::runtime_error("failed to create instance!");
  }

  checkGflwRequiredInstanceExtensions(context);
}
void setupDebugMessenger(EngineContext &context) {
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
unsigned int getDeviceScore(EngineContext &context, VkPhysicalDevice device) {
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

  VkPhysicalDeviceProperties properties;
  vkGetPhysicalDeviceProperties(device, &properties);

  if (!(indices.isComplete() && extensionsSupported && swapChainAdequate &&
        supportedFeatures.samplerAnisotropy &&
        supportedFeatures.tessellationShader &&
        supportedFeatures.fillModeNonSolid))
    return 0; // unsuitable

  unsigned int score = 1;
  score += (properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU);
  return score;
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

  if (context.vulkan.physicalDevice == VK_NULL_HANDLE || !maxScore) {
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
    queueCreateInfos.emplace_back(VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
                                  nullptr, 0, queueFamily, 1, &queuePriority);
  }

  VkPhysicalDeviceRobustness2FeaturesKHR robustness2Features{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_ROBUSTNESS_2_FEATURES_KHR,
      .nullDescriptor = VK_TRUE};
  VkPhysicalDeviceDescriptorIndexingFeatures deviceDescriptorIndexingFeatures{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_DESCRIPTOR_INDEXING_FEATURES,
      .pNext = &robustness2Features,
      .descriptorBindingPartiallyBound = true};

  VkPhysicalDeviceFeatures deviceFeatures{.tessellationShader = VK_TRUE,
                                          .fillModeNonSolid = VK_TRUE,
                                          .samplerAnisotropy = VK_TRUE};
  VkPhysicalDeviceFeatures2 deviceFeatures2{
      .sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2,
      .pNext = &deviceDescriptorIndexingFeatures,
      .features = deviceFeatures};

  VkDeviceCreateInfo createInfo = {.sType =
                                       VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};

  createInfo.queueCreateInfoCount =
      static_cast<uint32_t>(queueCreateInfos.size());
  createInfo.pQueueCreateInfos = queueCreateInfos.data();

  createInfo.pNext = &deviceFeatures2;
  createInfo.enabledExtensionCount =
      static_cast<uint32_t>(deviceExtensions.size());
  createInfo.ppEnabledExtensionNames = deviceExtensions.data();

  // might not really be necessary anymore because device specific validation
  // layers have been deprecated
#ifndef NDEBUG
  createInfo.enabledLayerCount = static_cast<uint32_t>(validationLayers.size());
  createInfo.ppEnabledLayerNames = validationLayers.data();
#endif

  if (vkCreateDevice(context.vulkan.physicalDevice, &createInfo, nullptr,
                     &context.vulkan.device) != VK_SUCCESS) {
    throw std::runtime_error("failed to create logical device!");
  }

#ifndef NDEBUG
  context.vulkan.debug.setObjName =
      reinterpret_cast<PFN_vkSetDebugUtilsObjectNameEXT>(vkGetDeviceProcAddr(
          context.vulkan.device, "vkSetDebugUtilsObjectNameEXT"));
  context.vulkan.debug.beginLabel =
      reinterpret_cast<PFN_vkCmdBeginDebugUtilsLabelEXT>(vkGetDeviceProcAddr(
          context.vulkan.device, "vkCmdBeginDebugUtilsLabelEXT"));
  context.vulkan.debug.endLabel =
      reinterpret_cast<PFN_vkCmdEndDebugUtilsLabelEXT>(vkGetDeviceProcAddr(
          context.vulkan.device, "vkCmdEndDebugUtilsLabelEXT"));
  if (!context.vulkan.debug.setObjName || !context.vulkan.debug.beginLabel ||
      !context.vulkan.debug.endLabel) {
    throw std::runtime_error("Failed to load VK_EXT_debug_utils device "
                             "functions. Is the extension enabled?");
  }
#endif
  debug::setObjName(context, VK_OBJECT_TYPE_DEVICE,
                    reinterpret_cast<uint64_t>(context.vulkan.device),
                    "logical device");

  vkGetDeviceQueue(context.vulkan.device, indices.graphicsFamily, 0,
                   &context.vulkan.graphicsQueue);
  debug::setObjName(context, VK_OBJECT_TYPE_QUEUE,
                    reinterpret_cast<uint64_t>(context.vulkan.graphicsQueue),
                    "graphics queue");
  vkGetDeviceQueue(context.vulkan.device, indices.computeFamily, 0,
                   &context.vulkan.computeQueue);
  debug::setObjName(context, VK_OBJECT_TYPE_QUEUE,
                    reinterpret_cast<uint64_t>(context.vulkan.computeQueue),
                    "compute queue");
  vkGetDeviceQueue(context.vulkan.device, indices.presentFamily, 0,
                   &context.vulkan.presentQueue);
  debug::setObjName(context, VK_OBJECT_TYPE_QUEUE,
                    reinterpret_cast<uint64_t>(context.vulkan.presentQueue),
                    "present queue");
}
void createCommandPool(EngineContext &context) {
  QueueFamilyIndices queueFamilyIndices =
      findQueueFamilies(context, context.vulkan.physicalDevice);

  VkCommandPoolCreateInfo poolInfo = {
      .sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
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
  std::println("{:=<60}", ""); // Table border

  // Header row
  std::println("{:<30} {:<25}", "Property", "Value");
  std::println("{:-<60}", ""); // Divider

  // Device Info
  std::println("{:<30} {:<25}", "Selected GPU",
               context.vulkan.physicalDeviceProperties.deviceName);
  std::println(
      "{:<30} {}.{}.{}", "API Version",
      VK_VERSION_MAJOR(context.vulkan.physicalDeviceProperties.apiVersion),
      VK_VERSION_MINOR(context.vulkan.physicalDeviceProperties.apiVersion),
      VK_VERSION_PATCH(context.vulkan.physicalDeviceProperties.apiVersion));

  std::println("Max frames in flight: {}", context.vulkan.maxFramesInFlight);

  std::println("{:=<60}", ""); // Table border
}
void setupGlobResources(EngineContext &context) {
  std::vector<DescriptorAllocatorGrowable::PoolSizeRatio> poolSizeRatios = {
      {VK_DESCRIPTOR_TYPE_STORAGE_IMAGE, 3},
      {VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3},
      {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, 3},
      {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 4},
  };
  context.vulkan.globalDescriptorAllocator =
      std::make_unique<DescriptorAllocatorGrowable>(context);
  context.vulkan.globalDescriptorAllocator->init(100, poolSizeRatios);

  context.vulkan.globalUBOs.reserve(context.vulkan.maxFramesInFlight);
  for (int i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    auto &buf = context.vulkan.globalUBOs.emplace_back(
        context, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT);
    buf.map();
  }

  context.vulkan.globalDescriptorSetLayout = buildDescriptorSetLayout(
      context, {VkDescriptorSetLayoutBinding{
                   .binding = 0,
                   .descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER,
                   .descriptorCount = 1,
                   .stageFlags = VK_SHADER_STAGE_ALL,
               }});

  context.vulkan.globalDescriptorSets.reserve(context.vulkan.maxFramesInFlight);
  for (int i = 0; i < context.vulkan.maxFramesInFlight; i++) {
    auto &set = context.vulkan.globalDescriptorSets.emplace_back();
    set = context.vulkan.globalDescriptorAllocator->allocate(
        context.vulkan.globalDescriptorSetLayout);
    DescriptorWriter writer(context);
    writer.writeBuffer(0, context.vulkan.globalUBOs[i].descriptorInfo(),
                       VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER);
    writer.updateSet(set);
  }
}
void init(EngineContext &context) {
  createInstance(context);

#ifndef NDEBUG
  setupDebugMessenger(context);
#endif

  glfwCreateWindowSurface(context.vulkan.instance, context.window, nullptr,
                          &context.vulkan.surface);
  pickPhysicalDevice(context);
  createLogicalDevice(context);
  debug::setObjName(context, VK_OBJECT_TYPE_INSTANCE,
                    reinterpret_cast<uint64_t>(context.vulkan.instance),
                    "instance");

  auto swapChainSupport = getSwapChainSupport(context);
  uint32_t imageCount = (swapChainSupport.capabilities.minImageCount <= 2)
                            ? 2
                            : swapChainSupport.capabilities.minImageCount + 1;
  // maxImageCount 0 means infinite
  if (swapChainSupport.capabilities.maxImageCount != 0 &&
      imageCount > swapChainSupport.capabilities.maxImageCount)
    imageCount = swapChainSupport.capabilities.maxImageCount;

  context.vulkan.maxFramesInFlight = imageCount;

  createCommandPool(context);
  displayInitInfo(context);

  VkPhysicalDeviceProperties properties{};
  vkGetPhysicalDeviceProperties(context.vulkan.physicalDevice, &properties);

  VkSamplerCreateInfo samplerInfo{.sType =
                                      VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
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
                      &context.vulkan.defaultSampler) != VK_SUCCESS) {
    throw std::runtime_error("failed to create texture sampler!");
  }

  setupGlobResources(context);
}
} // namespace vkh
