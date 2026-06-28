#include "deviceHelpers.hpp"

#include <fstream>
#include <vector>
#include <vulkan/vulkan.hpp>

#include "swapChain.hpp"

namespace vkh {

SwapChainSupportDetails getSwapChainSupport(EngineContext &context) {
  return querySwapChainSupport(context, context.vulkan.physicalDevice);
}

QueueFamilyIndices findPhysicalQueueFamilies(EngineContext &context) {
  return findQueueFamilies(context, context.vulkan.physicalDevice);
}

SwapChainSupportDetails querySwapChainSupport(EngineContext &context,
                                              vk::PhysicalDevice device) {
  SwapChainSupportDetails details;
  details.capabilities =
      device.getSurfaceCapabilitiesKHR(context.vulkan.surface);
  details.formats = device.getSurfaceFormatsKHR(context.vulkan.surface);
  details.presentModes =
      device.getSurfacePresentModesKHR(context.vulkan.surface);

  return details;
}

QueueFamilyIndices findQueueFamilies(EngineContext &context,
                                     vk::PhysicalDevice device) {
  QueueFamilyIndices indices;

  auto queueFamilies = device.getQueueFamilyProperties();

  int i = 0;
  for (const auto &queueFamily : queueFamilies) {
    if (queueFamily.queueCount > 0 &&
        (queueFamily.queueFlags & vk::QueueFlagBits::eGraphics)) {
      indices.graphicsFamily = i;
      indices.graphicsFamilyHasValue = true;
    }

    vk::Bool32 presentSupport =
        device.getSurfaceSupportKHR(i, context.vulkan.surface);
    if (queueFamily.queueCount > 0 && presentSupport) {
      indices.presentFamily = i;
      indices.presentFamilyHasValue = true;
    }

    if (queueFamily.queueCount > 0 &&
        (queueFamily.queueFlags & vk::QueueFlagBits::eCompute)) {
      indices.computeFamily = i;
      indices.computeFamilyHasValue = true;
    }

    if (indices.isComplete()) {
      break;
    }

    i++;
  }

  return indices;
}

uint32_t findMemoryType(EngineContext &context, uint32_t typeFilter,
                        vk::MemoryPropertyFlags properties) {
  vk::PhysicalDeviceMemoryProperties memProperties =
      context.vulkan.physicalDevice.getMemoryProperties();

  for (uint32_t i = 0; i < memProperties.memoryTypeCount; i++) {
    if ((typeFilter & (1 << i)) && (memProperties.memoryTypes[i].propertyFlags &
                                    properties) == properties) {
      return i;
    }
  }

  throw std::runtime_error("failed to find suitable memory type!");
}

vk::Format findSupportedFormat(EngineContext &context,
                               const std::vector<vk::Format> &candidates,
                               vk::ImageTiling tiling,
                               vk::FormatFeatureFlags features) {
  for (vk::Format format : candidates) {
    vk::FormatProperties props =
        context.vulkan.physicalDevice.getFormatProperties(format);

    if (tiling == vk::ImageTiling::eLinear &&
        (props.linearTilingFeatures & features) == features) {
      return format;
    } else if (tiling == vk::ImageTiling::eOptimal &&
               (props.optimalTilingFeatures & features) == features) {
      return format;
    }
  }
  throw std::runtime_error("failed to find supported format!");
}

void createBuffer(EngineContext &context, vk::DeviceSize size,
                  vk::BufferUsageFlags usage,
                  vk::MemoryPropertyFlags properties, vk::Buffer &buffer,
                  vk::DeviceMemory &bufferMemory) {
  vk::BufferCreateInfo bufferInfo{{},    // flags
                                  size,  // size
                                  usage, // usage
                                  vk::SharingMode::eExclusive};

  try {
    buffer = context.vulkan.device.createBuffer(bufferInfo);
  } catch (const vk::SystemError &) {
    throw std::runtime_error("failed to create vertex buffer!");
  }

  vk::MemoryRequirements memRequirements =
      context.vulkan.device.getBufferMemoryRequirements(buffer);

  vk::MemoryAllocateInfo allocInfo{
      memRequirements.size,
      findMemoryType(context, memRequirements.memoryTypeBits, properties)};

  try {
    bufferMemory = context.vulkan.device.allocateMemory(allocInfo);
  } catch (const vk::SystemError &) {
    throw std::runtime_error("failed to allocate vertex buffer memory!");
  }

  context.vulkan.device.bindBufferMemory(buffer, bufferMemory, 0);
}

vk::CommandBuffer beginSingleTimeCommands(EngineContext &context) {
  vk::CommandBufferAllocateInfo allocInfo{
      context.vulkan.commandPool, vk::CommandBufferLevel::ePrimary,
      1 // count
  };

  vk::CommandBuffer commandBuffer;
  try {
    std::vector<vk::CommandBuffer> bufs =
        context.vulkan.device.allocateCommandBuffers(allocInfo);
    commandBuffer = bufs[0];
  } catch (const vk::SystemError &) {
    throw std::runtime_error("failed to allocate command buffers!");
  }

  vk::CommandBufferBeginInfo beginInfo{
      vk::CommandBufferUsageFlagBits::eOneTimeSubmit};

  commandBuffer.begin(beginInfo);
  return commandBuffer;
}

void endSingleTimeCommands(EngineContext &context,
                           vk::CommandBuffer commandBuffer, vk::Queue queue) {
  commandBuffer.end();

  vk::SubmitInfo submitInfo{
      0, nullptr, nullptr, // Wait semaphores
      1, &commandBuffer    // Command buffers
  };

  if (queue.submit(1, &submitInfo, nullptr) != vk::Result::eSuccess)
    throw std::runtime_error(
        "Failed to submit single time command buffer to queue");
  queue.waitIdle();

  context.vulkan.device.freeCommandBuffers(context.vulkan.commandPool, 1,
                                           &commandBuffer);
}

void copyBuffer(EngineContext &context, vk::Buffer srcBuffer,
                vk::Buffer dstBuffer, vk::DeviceSize size) {
  vk::CommandBuffer commandBuffer = beginSingleTimeCommands(context);

  vk::BufferCopy copyRegion{
      0,   // srcOffset
      0,   // dstOffset
      size // size
  };
  commandBuffer.copyBuffer(srcBuffer, dstBuffer, 1, &copyRegion);

  endSingleTimeCommands(context, commandBuffer, context.vulkan.graphicsQueue);
}

void copyBufferToImage(EngineContext &context, vk::Buffer buffer,
                       vk::Image image, uint32_t width, uint32_t height,
                       uint32_t offset) {
  vk::CommandBuffer commandBuffer = beginSingleTimeCommands(context);

  vk::BufferImageCopy region{
      offset, // bufferOffset
      0,      // bufferRowLength
      0,      // bufferImageHeight
      vk::ImageSubresourceLayers{vk::ImageAspectFlagBits::eColor, 0, 0, 1},
      vk::Offset3D{0, 0, 0},
      vk::Extent3D{width, height, 1}};

  commandBuffer.copyBufferToImage(
      buffer, image, vk::ImageLayout::eTransferDstOptimal, 1, &region);

  endSingleTimeCommands(context, commandBuffer, context.vulkan.graphicsQueue);
}

vk::ImageView createImageView(EngineContext &context, vk::Image image,
                              vk::Format format) {
  vk::ImageViewCreateInfo viewInfo{
      {}, // flags
      image,
      vk::ImageViewType::e2D,
      format,
      vk::ComponentMapping{},
      vk::ImageSubresourceRange{vk::ImageAspectFlagBits::eColor, 0, 1, 0, 1}};

  vk::ImageView imageView;
  try {
    imageView = context.vulkan.device.createImageView(viewInfo);
  } catch (const vk::SystemError &) {
    throw std::runtime_error("failed to create texture image view!");
  }
  return imageView;
}

vk::ImageView createTextureImageView(EngineContext &context, vk::Image image) {
  return createImageView(context, image,
                         context.vulkan.swapChain->getSwapChainImageFormat());
}

std::vector<char> readFile(const std::filesystem::path &filepath) {
  std::ifstream file{filepath, std::ios::ate | std::ios::binary};

  if (!file.is_open()) {
    throw std::runtime_error("failed to open file: " + filepath.string());
  }

  size_t fileSize = static_cast<size_t>(file.tellg());
  std::vector<char> buffer(fileSize);

  file.seekg(0);
  file.read(buffer.data(), fileSize);

  file.close();
  return buffer;
}
void writeFile(const std::filesystem::path &filepath, const void *data,
               size_t size) {
  if (filepath.has_parent_path()) {
    std::filesystem::create_directories(filepath.parent_path());
  }

  std::ofstream file{filepath, std::ios::binary};
  if (!file.is_open()) {
    throw std::runtime_error("failed to open file for writing: " +
                             filepath.string());
  }

  file.write(reinterpret_cast<const char *>(data), size);
  file.close();
}

vk::Image createImageWithInfo(EngineContext &context,
                              const vk::ImageCreateInfo &imageInfo,
                              vk::DeviceMemory &imageMemory) {
  vk::Image image;
  try {
    image = context.vulkan.device.createImage(imageInfo);
  } catch (const vk::SystemError &) {
    throw std::runtime_error("failed to create image!");
  }

  vk::MemoryRequirements memRequirements =
      context.vulkan.device.getImageMemoryRequirements(image);

  vk::MemoryAllocateInfo allocInfo{
      memRequirements.size,
      findMemoryType(context, memRequirements.memoryTypeBits,
                     vk::MemoryPropertyFlagBits::eDeviceLocal)};

  try {
    imageMemory = context.vulkan.device.allocateMemory(allocInfo);
  } catch (const vk::SystemError &) {
    throw std::runtime_error("failed to allocate image memory!");
  }

  context.vulkan.device.bindImageMemory(image, imageMemory, 0);
  return image;
}

size_t getNonCoherentAtomSizeAlignment(EngineContext &context,
                                       size_t originalSize) {
  const size_t kAtomSize =
      context.vulkan.physicalDeviceProperties.limits.nonCoherentAtomSize;
  size_t alignedSize = originalSize;
  if (kAtomSize > 0) {
    alignedSize = (alignedSize + kAtomSize - 1) & ~(kAtomSize - 1);
  }
  return alignedSize;
}

size_t getUniformBufferAlignment(EngineContext &context, size_t originalSize) {
  const size_t minUboAlignment = context.vulkan.physicalDeviceProperties.limits
                                     .minUniformBufferOffsetAlignment;
  size_t alignedSize = originalSize;
  if (minUboAlignment > 0) {
    alignedSize = (alignedSize + minUboAlignment - 1) & ~(minUboAlignment - 1);
  }
  return alignedSize;
}

} // namespace vkh
