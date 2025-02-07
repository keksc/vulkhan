#include "water.hpp"

#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <GLFW/glfw3.h>
#include <fmt/format.h>
#include <glm/glm.hpp>
#include <glm/gtc/constants.hpp>
#include <glm/gtc/quaternion.hpp>
#include <vulkan/vulkan_core.h>

#include <complex>
#include <memory>
#include <random>
#include <stdexcept>

#include "../buffer.hpp"
#include "../descriptors.hpp"
#include "../deviceHelpers.hpp"
#include "../entity.hpp"
#include "../image.hpp"
#include "../pipeline.hpp"
#include "../renderer.hpp"

namespace vkh {
namespace waterSys {
std::unique_ptr<GraphicsPipeline> graphicsPipeline;
VkPipelineLayout graphicsPipelineLayout;
std::unique_ptr<DescriptorSetLayout> graphicsDescriptorSetLayout;
VkDescriptorSet graphicsDescriptorSet;

std::unique_ptr<Pipeline> computePipeline;
VkPipelineLayout computePipelineLayout;
std::unique_ptr<DescriptorSetLayout> computeDescriptorSetLayout;
VkDescriptorSet computeDescriptorSet;

struct PushConstantData {
  glm::mat4 modelMatrix{1.f};
  float time;
};

struct ComputePushConstantData {
  float time;
  float windSpeed;
  float A;
};

struct Vertex {
  glm::vec3 position{};
  glm::vec2 uv{};
};

std::vector<VkVertexInputAttributeDescription> attributeDescriptions = {
    {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
    {1, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)}};
std::vector<VkVertexInputBindingDescription> bindingDescriptions = {
    {0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX}};

std::unique_ptr<Buffer> vertexBuffer;
std::unique_ptr<Buffer> indexBuffer;

std::unique_ptr<Image> frequencyImage;
std::unique_ptr<Image> fftImage;
std::unique_ptr<Image> heightFieldImage;

std::vector<Vertex> vertices;

std::vector<uint32_t> indices;

void createVertexBuffer(EngineContext &context) {
  uint32_t count = static_cast<uint32_t>(vertices.size());
  VkDeviceSize bufSize = sizeof(vertices[0]) * count;
  uint32_t size = sizeof(vertices[0]);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = size;
  bufInfo.instanceCount = count;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(vertices.data());
  stagingBuffer.unmap();

  bufInfo.usage =
      VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  vertexBuffer = std::make_unique<Buffer>(context, bufInfo);

  copyBuffer(context, stagingBuffer, *vertexBuffer, bufSize);
}
void createIndexBuffer(EngineContext &context) {
  uint32_t count = indices.size();
  VkDeviceSize bufSize = sizeof(indices[0]) * count;
  uint32_t size = sizeof(indices[0]);

  BufferCreateInfo bufInfo{};
  bufInfo.instanceSize = size;
  bufInfo.instanceCount = count;
  bufInfo.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
                             VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
  Buffer stagingBuffer(context, bufInfo);
  stagingBuffer.map();
  stagingBuffer.write(indices.data());
  stagingBuffer.unmap();

  bufInfo.usage =
      VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT;
  bufInfo.memoryProperties = VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT;
  indexBuffer = std::make_unique<Buffer>(context, bufInfo);

  copyBuffer(context, stagingBuffer, *indexBuffer, bufSize);
}
void createDescriptors(EngineContext &context) {
  graphicsDescriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                      VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
          .build();
  VkDescriptorImageInfo imageInfo{};
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
  imageInfo.imageView = frequencyImage->getImageView();
  imageInfo.sampler = context.vulkan.fontSampler;
  DescriptorWriter(*graphicsDescriptorSetLayout, *context.vulkan.globalPool)
      .writeImage(0, &imageInfo)
      .build(graphicsDescriptorSet);

  computeDescriptorSetLayout =
      DescriptorSetLayout::Builder(context)
          .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_IMAGE,
                      VK_SHADER_STAGE_COMPUTE_BIT)
          .build();
  imageInfo.imageView = fftImage->getImageView();
  imageInfo.imageLayout = VK_IMAGE_LAYOUT_GENERAL;
  imageInfo.sampler = VK_NULL_HANDLE;
  DescriptorWriter(*computeDescriptorSetLayout, *context.vulkan.globalPool)
      .writeImage(0, &imageInfo)
      .build(computeDescriptorSet);
}

void createPipelineLayout(EngineContext &context) {
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.stageFlags = VK_SHADER_STAGE_VERTEX_BIT;
  pushConstantRange.offset = 0;
  pushConstantRange.size = sizeof(PushConstantData);

  std::vector<VkDescriptorSetLayout> descriptorSetLayouts{
      context.vulkan.globalDescriptorSetLayout->getDescriptorSetLayout(),
      graphicsDescriptorSetLayout->getDescriptorSetLayout()};

  VkPipelineLayoutCreateInfo pipelineLayoutInfo{};
  pipelineLayoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &graphicsPipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create pipeline layout!");

  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  pushConstantRange.size = sizeof(ComputePushConstantData);

  descriptorSetLayouts.clear();
  descriptorSetLayouts = {computeDescriptorSetLayout->getDescriptorSetLayout()};
  pipelineLayoutInfo.setLayoutCount =
      static_cast<uint32_t>(descriptorSetLayouts.size());
  pipelineLayoutInfo.pSetLayouts = descriptorSetLayouts.data();
  pipelineLayoutInfo.pushConstantRangeCount = 1;
  pipelineLayoutInfo.pPushConstantRanges = &pushConstantRange;
  if (vkCreatePipelineLayout(context.vulkan.device, &pipelineLayoutInfo,
                             nullptr, &computePipelineLayout) != VK_SUCCESS)
    throw std::runtime_error("failed to create compute pipeline layout!");
}
void createPipeline(EngineContext &context) {
  PipelineCreateInfo pipelineConfig{};
  pipelineConfig.pipelineLayout = graphicsPipelineLayout;
  pipelineConfig.renderPass = renderer::getSwapChainRenderPass(context);
  pipelineConfig.attributeDescriptions = attributeDescriptions;
  pipelineConfig.bindingDescriptions = bindingDescriptions;
  graphicsPipeline = std::make_unique<GraphicsPipeline>(
      context, "shaders/water.vert.spv", "shaders/water.frag.spv",
      pipelineConfig);

  computePipeline = std::make_unique<ComputePipeline>(
      context, "shaders/waterfreq.comp.spv", computePipelineLayout);
}
constexpr int N = 512;
const float GRID_SCALE = 10.f;
void generateGrid() {
  for (int i = 0; i < N; ++i) {
    for (int j = 0; j < N; ++j) {
      Vertex v;
      v.position.x = ((i / (N - 1.0f)) * 2.0f - 1.0f) * GRID_SCALE;
      v.position.z = ((j / (N - 1.0f)) * 2.0f - 1.0f) * GRID_SCALE;
      v.position.y = GROUND_LEVEL + 10.f;
      v.uv = glm::vec2(i / (N - 1.0f), j / (N - 1.0f));
      vertices.push_back(v);
    }
  }

  for (int i = 0; i < N - 1; ++i) {
    for (int j = 0; j < N - 1; ++j) {
      uint32_t topLeft = i * N + j;
      uint32_t topRight = topLeft + 1;
      uint32_t bottomLeft = (i + 1) * N + j;
      uint32_t bottomRight = bottomLeft + 1;

      indices.insert(indices.end(), {topLeft, bottomLeft, topRight});
      indices.insert(indices.end(), {topRight, bottomLeft, bottomRight});
    }
  }
}
constexpr float g = 9.81f;
constexpr float L = 1000.f;            // domain length (example)
constexpr float dk = (2.f * M_PI) / L; // wave number resolution
constexpr float gamma = 3.3f;
constexpr float A = 1.0f; // amplitude scaling factor

// Define ω_p as a function of wind speed (example value)
// For instance, you can set ω_p = 0.84 * g / windSpeed.
float computeOmegaP(float windSpeed) { return 0.84f * g / windSpeed; }

// Compute dispersion relation: ω(k) = sqrt(g * |k|)
float dispersion(const float kx, const float ky) {
  return std::sqrt(g * std::sqrt(kx * kx + ky * ky));
}

// Compute the JONSWAP spectrum S(k) for a given wavevector
float jonswapSpectrum(float kx, float ky, float windSpeed) {
  float kLength = std::sqrt(kx * kx + ky * ky);
  if (kLength < 1e-6f)
    return 0.f;

  float omega = dispersion(kx, ky);
  float omegaP = computeOmegaP(windSpeed);
  // Choose sigma based on whether omega is less or greater than ω_p:
  float sigma = (omega <= omegaP) ? 0.07f : 0.09f;

  // The base spectrum factor. (This form may vary in the literature.)
  float base = A * std::exp(-1.25f * std::pow(omegaP / omega, 4.f));

  // The peak enhancement term.
  float r = std::exp(-std::pow(omega - omegaP, 2.f) /
                     (2.f * sigma * sigma * omegaP * omegaP));
  float enhancement = std::pow(gamma, r);

  return base * enhancement;
}

struct ComplexField {
  std::vector<std::complex<float>> data;
  int size;
  ComplexField(int N) : data(N * N), size(N) {}
};

std::complex<float> generateGaussian(std::mt19937 &rng,
                                     std::normal_distribution<float> &dist) {
  return {dist(rng), dist(rng)};
}

void calculateInitialSpectrum(ComplexField &H0, float windSpeed) {
  std::random_device rd;
  std::mt19937 rng(rd());
  std::normal_distribution<float> norm(0.f, 1.f);

  // Loop over each grid point
  for (int m = 0; m < N; ++m) {
    // Map index m to wave number in x-direction.
    float kx = (m - N / 2) * dk;
    for (int n = 0; n < N; ++n) {
      // Map index n to wave number in y-direction.
      float ky = (n - N / 2) * dk;

      // Compute S(k) using JONSWAP.
      float S = jonswapSpectrum(kx, ky, windSpeed);

      // Generate a complex Gaussian random number.
      std::complex<float> xi = generateGaussian(rng, norm);

      // Compute H0(k) = sqrt(S(k)/2) * xi.
      std::complex<float> H0_k = std::sqrt(S * 0.5f) * xi;

      // Store in the array.
      H0.data[m * N + n] = H0_k;
    }
  }
}
std::vector<float> convertSpectrumToImageData(const ComplexField &H0) {
  // Each texel now needs 2 floats.
  std::vector<float> imageData(H0.size * H0.size * 2);
  for (int i = 0; i < H0.size; ++i) {
    for (int j = 0; j < H0.size; ++j) {
      std::complex<float> h = H0.data[i * H0.size + j];
      // Store real part in first channel, imaginary part in second.
      imageData[(i * H0.size + j) * 2 + 0] = h.real();
      imageData[(i * H0.size + j) * 2 + 1] = h.imag();
    }
  }
  return imageData;
}
void createImages(EngineContext &context) {
  ImageCreateInfo imageInfo{};
  imageInfo.w = imageInfo.h = N;
  imageInfo.format = VK_FORMAT_R32G32_SFLOAT;
  ComplexField H0(N);
  float windSpeed = 10.f;
  calculateInitialSpectrum(H0, windSpeed);
  imageInfo.data = convertSpectrumToImageData(H0).data();
  frequencyImage = std::make_unique<Image>(context, imageInfo);
  imageInfo.layout = VK_IMAGE_LAYOUT_GENERAL;
  imageInfo.usage =
      VK_IMAGE_USAGE_TRANSFER_DST_BIT | VK_IMAGE_USAGE_STORAGE_BIT;
  fftImage = std::make_unique<Image>(context, imageInfo);
  imageInfo.format = VK_FORMAT_R32_SFLOAT;
  heightFieldImage = std::make_unique<Image>(context, imageInfo);
}
void init(EngineContext &context) {

  generateGrid();
  createVertexBuffer(context);
  createIndexBuffer(context);

  createImages(context);

  createDescriptors(context);
  createPipelineLayout(context);
  createPipeline(context);
}

void cleanup(EngineContext &context) {
  vertexBuffer = nullptr;
  indexBuffer = nullptr;
  graphicsPipeline = nullptr;
  frequencyImage = nullptr;
  fftImage = nullptr;
  heightFieldImage = nullptr;
  graphicsDescriptorSetLayout = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, graphicsPipelineLayout,
                          nullptr);

  computePipeline = nullptr;
  computeDescriptorSetLayout = nullptr;
  vkDestroyPipelineLayout(context.vulkan.device, computePipelineLayout,
                          nullptr);
}

void dispatchCompute(EngineContext &context) {
  computePipeline->bind(context.frameInfo.commandBuffer);
}
void render(EngineContext &context) {
  // transitionImageLayout(context, heightFieldImage->getImage(),
  // VK_FORMAT_R32_SFLOAT, VK_IMAGE_LAYOUT_GENERAL,
  // VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);

  graphicsPipeline->bind(context.frameInfo.commandBuffer);

  PushConstantData push{};
  Transform transform{.position = {5.f, GROUND_LEVEL, 0.f}};
  push.modelMatrix = transform.mat4();
  push.time = glfwGetTime();

  vkCmdPushConstants(context.frameInfo.commandBuffer, graphicsPipelineLayout,
                     VK_SHADER_STAGE_VERTEX_BIT, 0, sizeof(PushConstantData),
                     &push);

  // model->bind(context, context.frameInfo.commandBuffer, pipelineLayout);
  // model->draw(context.frameInfo.commandBuffer);
  VkBuffer buffers[] = {*vertexBuffer};
  VkDeviceSize offsets[] = {0};
  vkCmdBindVertexBuffers(context.frameInfo.commandBuffer, 0, 1, buffers,
                         offsets);
  vkCmdBindIndexBuffer(context.frameInfo.commandBuffer, *indexBuffer, 0,
                       VK_INDEX_TYPE_UINT32);
  VkDescriptorSet descriptorSets[2] = {context.frameInfo.globalDescriptorSet,
                                       graphicsDescriptorSet};
  vkCmdBindDescriptorSets(
      context.frameInfo.commandBuffer, VK_PIPELINE_BIND_POINT_GRAPHICS,
      graphicsPipelineLayout, 0, 2, descriptorSets, 0, nullptr);

  vkCmdDrawIndexed(context.frameInfo.commandBuffer, indices.size(), 1, 0, 0, 0);
}
} // namespace waterSys
} // namespace vkh
