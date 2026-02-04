#pragma once

#include <glm/ext.hpp>
#include <glm/glm.hpp>
#include <vulkan/vulkan_core.h>

#include <complex>
#include <memory>
#include <vector>

#include "../../buffer.hpp"
#include "../../image.hpp"
#include "../system.hpp"

#include <vkFFT.h>

namespace vkh {
class WSTessendorf : public System {
public:
  WSTessendorf(EngineContext &context);
  ~WSTessendorf();

  void recordComputeWaves(VkCommandBuffer &cmd, float time);

  static constexpr unsigned int tileSize = 512; // has to be a power of 2
  static constexpr unsigned int tileSizeSquared = tileSize * tileSize;
  static constexpr float tileLength = 1000.f;

  Image &getDisplacementFoamImage() {
    return *displacementFoamMap;
  }

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
    float dt;
  };

  void createDescriptors();

  std::unique_ptr<Image> displacementFoamMap;
};
} // namespace vkh
