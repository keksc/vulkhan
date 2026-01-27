#pragma once

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include <complex>
#include <memory>
#include <random>
#include <vector>

#include "../../buffer.hpp"
#include "../system.hpp"

#include <vkFFT.h>

namespace vkh {
class WSTessendorf : public System {
public:
  WSTessendorf(EngineContext &context);
  ~WSTessendorf();

  float recordComputeWaves(VkCommandBuffer &cmd, float time);

  // has to be a power of 2
  const unsigned int tileSize = 512;
  const unsigned int tileSizeSquared = tileSize * tileSize;
  const float tileLength = 1000.0f;

  Buffer<glm::vec4> &getDisplacementsAndNormalsBuffer() {
    return *displacementsAndNormals;
  }
  float lambda{-1.0f}; ///< Importance of displacement vector

private:
  struct WaveVector {
    glm::vec2 vec;
    glm::vec2 unit;

    WaveVector(glm::vec2 v)
        : vec(v),
          unit(glm::length(v) > 0.00001f ? glm::normalize(v) : glm::vec2(0)) {}

    WaveVector(glm::vec2 v, glm::vec2 u) : vec(v), unit(u) {}
  };

  struct BaseWaveHeight {
    std::complex<float> heightAmp;      ///< FT amplitude of wave height
    std::complex<float> heightAmp_conj; ///< conjugate of wave height amplitude
    float dispersion;                   ///< Descrete dispersion value
    float _pad;                         // 4
  };
  static_assert(sizeof(BaseWaveHeight) == 24,
                "BaseWaveHeight must match std430!");

  void computeWaveVectors(std::vector<WaveVector> &waveVecs);
  // std::vector<BaseWaveHeight> computeBaseWaveHeightField(
  //     const std::vector<WaveVector> waveVectors) const;

  glm::vec2 m_WindDir = glm::vec2(.7071067812f); ///< Unit vector
  float m_WindSpeed{50.0f};

  // Phillips spectrum
  float m_Damping{0.1f};

  float m_AnimationPeriod;
  float m_BaseFreq{1.0f};

private:
  static std::complex<float> WaveHeightFT(const BaseWaveHeight &waveHeight,
                                          const float t) {
    const float omega_t = waveHeight.dispersion * t;

    // exp(ix) = cos(x) * i*sin(x)
    const float pcos = glm::cos(omega_t);
    const float psin = glm::sin(omega_t);

    return waveHeight.heightAmp * std::complex<float>(pcos, psin) +
           waveHeight.heightAmp_conj * std::complex<float>(pcos, -psin);
  }

  std::unique_ptr<Buffer<std::complex<float>>> FFTData;
  std::unique_ptr<Buffer<BaseWaveHeight>> baseWaveHeightField;
  std::unique_ptr<Buffer<WaveVector>> waveVectors;
  VkDescriptorSetLayout preFFTSetLayout;
  VkDescriptorSet preFFTSet;
  VkDescriptorSetLayout postFFTSetLayout;
  VkDescriptorSet postFFTSet;

  VkFFTApplication app{};

  VkFence fence;

  std::unique_ptr<ComputePipeline> preFFTPipeline;
  std::unique_ptr<ComputePipeline> postFFTPipeline;

  struct PushConstantData {
    float t;
  };

  void createDescriptors();

  std::unique_ptr<Buffer<float>> minReductionBuffer0;
  std::unique_ptr<Buffer<float>> maxReductionBuffer0;
  std::unique_ptr<Buffer<float>> minReductionBuffer1;
  std::unique_ptr<Buffer<float>> maxReductionBuffer1;
  std::unique_ptr<Buffer<float>> minReductionBuffer2;
  std::unique_ptr<Buffer<float>> maxReductionBuffer2;

  VkDescriptorSetLayout reductionSetLayout0;
  VkDescriptorSetLayout reductionSetLayout1;
  VkDescriptorSet reductionSet0;
  VkDescriptorSet reductionSet1;
  VkDescriptorSet reductionSet2;

  std::unique_ptr<ComputePipeline> reductionPipeline0;
  std::unique_ptr<ComputePipeline> reductionPipeline1;

  struct ReductionPushConstants {
    uint32_t inputSize;
  };

  std::unique_ptr<Buffer<float>> masterMinBuffer;
  std::unique_ptr<Buffer<float>> masterMaxBuffer;
  std::unique_ptr<Buffer<float>> scaleBuffer;

  VkDescriptorSetLayout updateSetLayout;
  VkDescriptorSet updateSet;
  VkDescriptorSetLayout normalizeSetLayout;
  VkDescriptorSet normalizeSet;

  std::unique_ptr<ComputePipeline> updatePipeline;
  std::unique_ptr<ComputePipeline> normalizePipeline;

  VkCommandBuffer preRecordedCmdBuffer{};
  std::unique_ptr<Buffer<glm::vec4>> displacementsAndNormals;
};
} // namespace vkh
