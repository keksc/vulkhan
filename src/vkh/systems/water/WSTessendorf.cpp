#include "WSTessendorf.hpp"

#include <fmt/format.h>
#include <vulkan/vulkan_core.h>

#include <limits>
#include <memory>
#include <algorithm>
#include <stdexcept>

#include "../../descriptors.hpp"
#include "../../deviceHelpers.hpp"

namespace vkh {
void WSTessendorf::gpuIfft2d(std::complex<float> *data) {}

void WSTessendorf::ifft1d(std::complex<float> *data) {
  const int N = tileSize;
  const float pi = glm::pi<float>();
  int j = 0;
  for (int i = 0; i < N - 1; i++) {
    if (i < j)
      std::swap(data[i], data[j]);
    int k = N >> 1;
    while (k <= j)
      j -= k, k >>= 1;
    j += k;
  }

  int mmax = 1;
  while (mmax < N) {
    int istep = mmax << 1;
    float theta = 2.0f * pi / istep;
    std::complex<float> wp(cos(theta), sin(theta));
    std::complex<float> w(1.0f, 0.0f);
    for (int m = 0; m < mmax; m++) {
      for (int i = m; i < N; i += istep) {
        int j = i + mmax;
        std::complex<float> temp = w * data[j];
        data[j] = data[i] - temp;
        data[i] += temp;
      }
      w *= wp;
    }
    mmax = istep;
  }
}

void WSTessendorf::ifft2d(std::complex<float> *data) {
  const int N = tileSize;
  for (int i = 0; i < N; i++)
    ifft1d(data + i * N);

  std::vector<std::complex<float>> col(N);
  for (int j = 0; j < N; j++) {
    for (int i = 0; i < N; i++)
      col[i] = data[i * N + j];
    ifft1d(col.data());
    for (int i = 0; i < N; i++)
      data[i * N + j] = col[i];
  }
}

void WSTessendorf::createDescriptors() {
  preFFTSetLayout = DescriptorSetLayout::Builder(context)
                        .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT)
                        .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT)
                        .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                    VK_SHADER_STAGE_COMPUTE_BIT)
                        .build();
  VkDescriptorBufferInfo preFFTDescriptorInfos[] = {
      FFTData->descriptorInfo(), baseWaveHeightField->descriptorInfo(),
      waveVectors->descriptorInfo()};
  DescriptorWriter(*preFFTSetLayout, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &preFFTDescriptorInfos[0])
      .writeBuffer(1, &preFFTDescriptorInfos[1])
      .writeBuffer(2, &preFFTDescriptorInfos[2])
      .build(preFFTSet);

  postFFTSetLayout = DescriptorSetLayout::Builder(context)
                         .addBinding(0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_SHADER_STAGE_COMPUTE_BIT)
                         .addBinding(2, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,
                                     VK_SHADER_STAGE_COMPUTE_BIT)
                         .build();
  VkDescriptorBufferInfo postFFTDescriptorInfos[] = {
      FFTData->descriptorInfo(), displacements->descriptorInfo(), normals->descriptorInfo()};
  DescriptorWriter(*postFFTSetLayout, *context.vulkan.globalDescriptorPool)
      .writeBuffer(0, &postFFTDescriptorInfos[0])
      .writeBuffer(1, &postFFTDescriptorInfos[1])
      .writeBuffer(2, &postFFTDescriptorInfos[2])
      .build(postFFTSet);
}

WSTessendorf::WSTessendorf(EngineContext &context) : System(context) {
  VkFenceCreateInfo fenceCreateInfo{};
  fenceCreateInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
  vkCreateFence(context.vulkan.device, &fenceCreateInfo, nullptr, &fence);

  FFTData = std::make_unique<Buffer<std::complex<float>>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      7 * tileSizeSquared);

  baseWaveHeightField = std::make_unique<Buffer<BaseWaveHeight>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared);

  waveVectors = std::make_unique<Buffer<WaveVector>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared);

  displacements = std::make_unique<Buffer<Displacement>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared);

  normals = std::make_unique<Buffer<Normal>>(
      context, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
      VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT |
          VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
      tileSizeSquared);

  VkFFTConfiguration config{};
  config.physicalDevice = &context.vulkan.physicalDevice;
  config.device = &context.vulkan.device;
  config.queue = &context.vulkan.computeQueue;
  config.commandPool = &context.vulkan.commandPool;
  config.isCompilerInitialized = 0;
  config.fence = &fence;
  config.FFTdim = 2;
  config.size[0] = tileSize;
  config.size[1] = tileSize;
  config.doublePrecision = 0;
  config.halfPrecision = 0;
  config.numberBatches = 7;
  config.useLUT = 0;
  config.performR2C = 0;
  config.performDCT = 0;
  config.disableReorderFourStep = 0;

  uint64_t bufSize =
      7 * config.size[0] * config.size[1] * sizeof(std::complex<float>);
  config.buffer = *FFTData;
  config.bufferSize = &bufSize;
  config.bufferNum = 1;
  config.makeInversePlanOnly = 1;

  VkFFTResult result = initializeVkFFT(&app, config);
  if (result)
    throw std::runtime_error(
        fmt::format("couldnt init VkFFT, error {}", static_cast<int>(result)));

  createDescriptors();

  VkPipelineLayoutCreateInfo layoutInfo{};
  layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
  layoutInfo.pSetLayouts = *preFFTSetLayout;
  layoutInfo.setLayoutCount = 1;
  VkPushConstantRange pushConstantRange{};
  pushConstantRange.size = sizeof(PushConstantData);
  pushConstantRange.stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
  layoutInfo.pushConstantRangeCount = 1;
  layoutInfo.pPushConstantRanges = &pushConstantRange;
  preFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/preFFT.comp.spv", layoutInfo);
  postFFTPipeline = std::make_unique<ComputePipeline>(
      context, "shaders/water/postFFT.comp.spv", layoutInfo);

  SetWindDirection(defaultWindDir);
  SetWindSpeed(defaultWindSpeed);
  SetAnimationPeriod(defaultAnimPeriod);
  SetDamping(defaultPhillipsDamping);

  std::vector<WaveVector> waveVecs;
  computeWaveVectors(waveVecs);
  std::vector<std::complex<float>> gaussRandomArr = ComputeGaussRandomArray();

  baseWaveHeightField->map();
  baseWaveHeightField->write(
      computeBaseWaveHeightField(waveVecs, gaussRandomArr).data());
  baseWaveHeightField->unmap();

  waveVectors->map();
  waveVectors->write(waveVecs.data());
  waveVectors->unmap();

  const Displacement kDefaultDisplacement{0.0};
  m_Displacements.resize(tileSizeSquared, kDefaultDisplacement);
  const Normal kDefaultNormal{0.0, 1.0, 0.0, 0.0};
  m_Normals.resize(tileSizeSquared, kDefaultNormal);
}

WSTessendorf::~WSTessendorf() {
  deleteVkFFT(&app);
  vkDestroyFence(context.vulkan.device, fence, nullptr);
}

void WSTessendorf::computeWaveVectors(std::vector<WaveVector> &waveVecs) {
  waveVecs.reserve(tileSizeSquared);

  for (int32_t m = 0; m < tileSize; ++m) {
    for (int32_t n = 0; n < tileSize; ++n) {
      waveVecs.emplace_back(
          glm::vec2(glm::pi<float>() * (2.0f * n - tileSize) / m_TileLength,
                    glm::pi<float>() * (2.0f * m - tileSize) / m_TileLength));
    }
  }
}

std::vector<std::complex<float>> WSTessendorf::ComputeGaussRandomArray() const {
  std::vector<std::complex<float>> randomArr(tileSizeSquared);
  for (int m = 0; m < tileSize; ++m)
    for (int n = 0; n < tileSize; ++n) {
      randomArr[m * tileSize + n] = std::complex<float>(
          glm::gaussRand(0.0f, 1.0f), glm::gaussRand(0.0f, 1.0f));
    }
  return randomArr;
}

std::vector<WSTessendorf::BaseWaveHeight>
WSTessendorf::computeBaseWaveHeightField(
    const std::vector<WaveVector> waveVectors,
    const std::vector<std::complex<float>> &gaussRandomArray) const {
  std::vector<WSTessendorf::BaseWaveHeight> baseWaveHeights(tileSize *
                                                            tileSize);
  assert(waveVectors.size() == baseWaveHeights.size());
  assert(baseWaveHeights.size() == gaussRandomArray.size());

  for (uint32_t m = 0; m < tileSize; ++m) {
    for (uint32_t n = 0; n < tileSize; ++n) {
      const uint32_t kIndex = m * tileSize + n;
      const auto &kWaveVec = waveVectors[kIndex];
      const float k = glm::length(kWaveVec.vec);
      auto &h0 = baseWaveHeights[kIndex];
      if (k > 0.00001f) {
        const auto gaussRandom = gaussRandomArray[kIndex];
        h0.heightAmp = BaseWaveHeightFT(gaussRandom, kWaveVec.unit, k);
        h0.heightAmp_conj =
            std::conj(BaseWaveHeightFT(gaussRandom, -kWaveVec.unit, k));
        h0.dispersion = QDispersion(k);
      } else {
        h0.heightAmp = std::complex<float>(0);
        h0.heightAmp_conj = std::conj(std::complex<float>(0));
        h0.dispersion = 0.0f;
      }
    }
  }
  return baseWaveHeights;
}

float WSTessendorf::computeWaves(float t) {
  auto cmd = beginSingleTimeCommands(context);

  PushConstantData data{t};
  vkCmdPushConstants(cmd, preFFTPipeline->getLayout(),
                     VK_SHADER_STAGE_COMPUTE_BIT, 0, sizeof(PushConstantData),
                     &data);
  preFFTPipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          preFFTPipeline->getLayout(), 0, 1, &preFFTSet,
                          0, nullptr);
  vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);

  VkMemoryBarrier memoryBarrier{};
  memoryBarrier.sType = VK_STRUCTURE_TYPE_MEMORY_BARRIER;
  memoryBarrier.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
  memoryBarrier.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  // Perform GPU-accelerated IFFT
  VkFFTLaunchParams launchParams{};
  launchParams.buffer = *FFTData;
  launchParams.commandBuffer = &cmd;
  VkFFTResult result = VkFFTAppend(&app, 1, &launchParams);

  vkCmdPipelineBarrier(cmd, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
                       VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, 0, 1,
                       &memoryBarrier, 0, nullptr, 0, nullptr);

  vkCmdPushConstants(cmd, postFFTPipeline->getLayout(),
                     VK_SHADER_STAGE_COMPUTE_BIT, 0,
                     sizeof(PushConstantData), &data);
  postFFTPipeline->bind(cmd);
  vkCmdBindDescriptorSets(cmd, VK_PIPELINE_BIND_POINT_COMPUTE,
                          postFFTPipeline->getLayout(), 0, 1, &postFFTSet, 0,
                          nullptr);
  vkCmdDispatch(cmd, tileSize / 16, tileSize / 16, 1);

  endSingleTimeCommands(context, cmd, context.vulkan.computeQueue);

  // Copy results back to CPU
  displacements->map();
  memcpy(m_Displacements.data(), displacements->getMappedAddr(),
         tileSizeSquared * sizeof(Displacement));
  displacements->unmap();
  
  float maxHeight = std::numeric_limits<float>::min();
  float minHeight = std::numeric_limits<float>::max();
  const float signs[] = {1.0f, -1.0f};

  for (uint32_t m = 0; m < tileSize; ++m) {
    for (uint32_t n = 0; n < tileSize; ++n) {
      const uint32_t idx = m * tileSize + n;
      float h_FT = m_Displacements[idx].y; // Height already computed in shader
      maxHeight = glm::max(h_FT, maxHeight);
      minHeight = glm::min(h_FT, minHeight);
    }
  }

  float masterMaxHeight = glm::max(maxHeight, masterMaxHeight);
  float masterMinHeight = glm::min(minHeight, masterMinHeight);

  normals->map();
  memcpy(m_Normals.data(), normals->getMappedAddr(),
         tileSizeSquared * sizeof(Normal));
  normals->unmap();

  return NormalizeHeights(masterMinHeight, masterMaxHeight);
}

float WSTessendorf::NormalizeHeights(float minHeight, float maxHeight) {
  const float A = glm::max(glm::abs(minHeight), glm::abs(maxHeight));
  const float OneOverA = 1.f / A;
  std::for_each(m_Displacements.begin(), m_Displacements.end(),
                [OneOverA](auto &d) { d.y *= OneOverA; });
  return A;
}

void WSTessendorf::SetWindDirection(const glm::vec2 &w) {
  m_WindDir = glm::normalize(w);
}
void WSTessendorf::SetWindSpeed(float v) { m_WindSpeed = glm::max(0.0001f, v); }
void WSTessendorf::SetAnimationPeriod(float T) {
  m_AnimationPeriod = T;
  m_BaseFreq = 2.0f * glm::pi<float>() / m_AnimationPeriod;
}
void WSTessendorf::SetLambda(float lambda) { m_Lambda = lambda; }
void WSTessendorf::SetDamping(float damping) { m_Damping = damping; }
} // namespace vkh
