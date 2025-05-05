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
/**
 * @brief Generates data used for rendering the water surface
 *  - displacements
 *      horizontal displacements along with vertical displacement = height,
 *  - normals
 *  Based on: Jerry Tessendorf. Simulating Ocean Water. 1999.
 *
 * Assumptions:
 *  1) Size of a tile (patch) of the surface is a power of two:
 *      size = size_X = size_Z
 *  2) A tile is a uniform grid of points and waves, number of points
 *      on the grid is same as the number of waves, and it is
 *      a power of two. Commonly used sizes: 256 or 512
 */
class WSTessendorf : public System {
public:
  static inline const glm::vec2 defaultWindDir{1.0f, 1.0f};
  static constexpr float defaultWindSpeed{30.0f};
  static constexpr float defaultAnimPeriod{200.0f};
  static constexpr float defaultPhillipsConst{3e-7f};
  static constexpr float defaultPhillipsDamping{0.1f};

  // Both vec4 due to GPU memory alignment requirements
  using Displacement = glm::vec4;
  using Normal = glm::vec4;

  /**
   * @brief Sets up the surface's properties.
   *  1. Call "Prepare()" to setup necessary structures for computation of waves
   *  2. Call "ComputeWaves()" fnc.
   * @param tileSize Size of the tile, is the number of points and waves,
   *  must be power of two
   * @param tileLength Length of tile, or wave length
   */
  WSTessendorf(EngineContext &context);
  ~WSTessendorf();

  /**
   * @brief Computes the wave height, horizontal displacement,
   *  and normal for each vertex. "Prepare()" must be called once before.
   * @param time Elapsed time in seconds
   * @return Amplitude of normalized heights (in <-1, 1> )
   */
  float computeWaves(float time);

  // ---------------------------------------------------------------------
  // Getters

  auto getTileSize() const { return tileSize; }
  auto getTileLength() const { return m_TileLength; }
  auto getWindDir() const { return m_WindDir; }
  auto getWindSpeed() const { return m_WindSpeed; }
  auto getAnimationPeriod() const { return m_AnimationPeriod; }
  auto getPhillipsConst() const { return m_A; }
  auto getDamping() const { return m_Damping; }
  auto getDisplacementLambda() const { return m_Lambda; }

  // Data

  size_t getDisplacementCount() const { return m_Displacements.size(); }
  const std::vector<Displacement> &getDisplacements() const {
    return m_Displacements;
  }

  size_t getNormalCount() const { return m_Normals.size(); }
  const std::vector<Normal> &getNormals() const { return m_Normals; }

  // ---------------------------------------------------------------------
  // Setters

  /** @param w Unit vector - direction of wind blowing */
  void SetWindDirection(const glm::vec2 &w);

  /** @param v Positive floating point number - speed of the wind in its
   * direction */
  void SetWindSpeed(float v);

  void SetAnimationPeriod(float T);

  /** @param lambda Importance of displacement vector */
  void SetLambda(float lambda);

  /** @param Damping Suppresses wave lengths smaller that its value */
  void SetDamping(float damping);

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
  std::vector<std::complex<float>> ComputeGaussRandomArray() const;
  std::vector<BaseWaveHeight> computeBaseWaveHeightField(
      const std::vector<WaveVector> waveVectors,
      const std::vector<std::complex<float>> &gaussRandomArray) const;

  /**
   * @brief Normalizes heights to interval <-1, 1>
   * @return Ampltiude, to reconstruct the orignal signal
   */
  float NormalizeHeights(float minHeight, float maxHeight);

  // ---------------------------------------------------------------------
  // Properties

  const uint32_t tileSize = 512;
  const uint32_t tileSizeSquared = tileSize * tileSize;
  const float m_TileLength = 1000.0f;

  glm::vec2 m_WindDir; ///< Unit vector
  float m_WindSpeed;

  // Phillips spectrum
  const float m_A = 3e-7f;
  float m_Damping;

  float m_AnimationPeriod;
  float m_BaseFreq{1.0f};

  float m_Lambda{-1.0f}; ///< Importance of displacement vector

  // -------------------------------------------------------------------------
  // Data

  // vec4(Displacement_X, height, Displacement_Z, [jacobian])
  std::vector<Displacement> m_Displacements;
  // vec4(slopeX, slopeZ, dDxdx, dDzdz )
  std::vector<Normal> m_Normals;

  // =========================================================================
  // Computation

private:
  static constexpr float s_kG{9.81}; ///< Gravitational constant
  static constexpr float s_kOneOver2sqrt{0.7071067812};

  /**
   * @brief Realization of water wave height field in fourier domain
   * @return Fourier amplitudes of a wave height field
   */
  std::complex<float> BaseWaveHeightFT(const std::complex<float> gaussRandom,
                                       const glm::vec2 unitWaveVec,
                                       float k) const {
    return s_kOneOver2sqrt * gaussRandom *
           glm::sqrt(PhillipsSpectrum(unitWaveVec, k));
  }

  /**
   * @brief Phillips spectrum - wave spectrum,
   *  a model for wind-driven waves larger than capillary waves
   */
  float PhillipsSpectrum(const glm::vec2 unitWaveVec, float k) const {
    const float k2 = k * k;
    const float k4 = k2 * k2;

    float cosFact = glm::dot(unitWaveVec, m_WindDir);
    cosFact = cosFact * cosFact;

    const float L = m_WindSpeed * m_WindSpeed / s_kG;
    const float L2 = L * L;

    return m_A * glm::exp(-1.0f / (k2 * L2)) / k4 * cosFact *
           glm::exp(-k2 * m_Damping * m_Damping);
  }

  static std::complex<float> WaveHeightFT(const BaseWaveHeight &waveHeight,
                                          const float t) {
    const float omega_t = waveHeight.dispersion * t;

    // exp(ix) = cos(x) * i*sin(x)
    const float pcos = glm::cos(omega_t);
    const float psin = glm::sin(omega_t);

    return waveHeight.heightAmp * std::complex<float>(pcos, psin) +
           waveHeight.heightAmp_conj * std::complex<float>(pcos, -psin);
  }

  // --------------------------------------------------------------------

  /**
   * @brief Computes quantization of the dispersion surface
   *  - of the frequencies of dispersion relation
   * @param k Magnitude of wave vector
   */
  inline float QDispersion(float k) const {
    return glm::floor(DispersionDeepWaves(k) / m_BaseFreq) * m_BaseFreq;
  }

  /**
   * @brief Dispersion relation for deep water, where the bottom may be
   *  ignored
   * @param k Magnitude of wave vector
   */
  inline float DispersionDeepWaves(float k) const {
    return glm::sqrt(s_kG * k);
  }

  /**
   * @brief Dispersion relation for waves where the bottom is relatively
   *  shallow compared to the length of the waves
   * @param k Magnitude of wave vector
   * @param D Depth below the mean water level
   */
  inline float DispersionTransWaves(float k, float D) const {
    return glm::sqrt(s_kG * k * glm::tanh(k * D));
  }

  /**
   * @brief Dispersion relation for very small waves, i.e., with
   *  wavelength of about >1 cm
   * @param k Magnitude of wave vector
   * @param L wavelength
   */
  inline float DispersionSmallWaves(float k, float L) const {
    return glm::sqrt(s_kG * k * (1 + k * k * L * L));
  }

  std::unique_ptr<Buffer<std::complex<float>>> FFTData;
  std::unique_ptr<Buffer<BaseWaveHeight>> baseWaveHeightField;
  std::unique_ptr<Buffer<WaveVector>> waveVectors;
  std::unique_ptr<Buffer<Displacement>> displacements;
  std::unique_ptr<Buffer<Normal>> normals;
  std::unique_ptr<DescriptorSetLayout> preFFTSetLayout;
  VkDescriptorSet preFFTSet;
  std::unique_ptr<DescriptorSetLayout> postFFTSetLayout;
  VkDescriptorSet postFFTSet;

  VkFFTApplication app{};

  VkFence fence;

  std::unique_ptr<ComputePipeline> preFFTPipeline;
  std::unique_ptr<ComputePipeline> postFFTPipeline;

  struct PushConstantData {
    float t;
  };

  void createDescriptors();
};
} // namespace vkh
