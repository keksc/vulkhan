#pragma once

#include <complex>
#include <memory>
#include <random>
#include <vector>

#include <glm/ext.hpp>
#include <glm/glm.hpp>

#include <vkFFT.h>
#include <fftw3.h>

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
class WSTessendorf {
public:
  static constexpr uint32_t defaultTileSize{512};
  static constexpr float defaultTileLength{1000.0f};

  static inline const glm::vec2 defaultWindDir{1.0f, 1.0f};
  static constexpr float defaultWindSpeed{30.0f};
  static constexpr float defaultAnimPeriod{200.0f};
  static constexpr float defaultPhillipsConst{3e-7f};
  static constexpr float defaultPhillipsDamping{0.1f};

  // Both vec4 due to GPU memory alignment requirements
  using Displacement = glm::vec4;
  using Normal = glm::vec4;

public:
  /**
   * @brief Sets up the surface's properties.
   *  1. Call "Prepare()" to setup necessary structures for computation of waves
   *  2. Call "ComputeWaves()" fnc.
   * @param tileSize Size of the tile, is the number of points and waves,
   *  must be power of two
   * @param tileLength Length of tile, or wave length
   */
  WSTessendorf(uint32_t tileSize = WSTessendorf::defaultTileSize,
               float tileLength = WSTessendorf::defaultTileLength);
  ~WSTessendorf();

  /**
   * @brief (Re)Creates necessary structures according to the previously set
   *  properties:
   *  a) Computes wave vectors
   *  b) Computes base wave height field amplitudes
   *  c) Sets up FFTW memory and plans
   */
  void Prepare();

  /**
   * @brief Computes the wave height, horizontal displacement,
   *  and normal for each vertex. "Prepare()" must be called once before.
   * @param time Elapsed time in seconds
   * @return Amplitude of normalized heights (in <-1, 1> )
   */
  float computeWaves(float time);

  // ---------------------------------------------------------------------
  // Getters

  auto getTileSize() const { return m_TileSize; }
  auto getTileLength() const { return m_TileLength; }
  auto getWindDir() const { return m_WindDir; }
  auto getWindSpeed() const { return m_WindSpeed; }
  auto getAnimationPeriod() const { return m_AnimationPeriod; }
  auto getPhillipsConst() const { return m_A; }
  auto getDamping() const { return m_Damping; }
  auto getDisplacementLambda() const { return m_Lambda; }
  float getMinHeight() const { return m_MinHeight; }
  float getMaxHeight() const { return m_MaxHeight; }

  // Data

  size_t getDisplacementCount() const { return m_Displacements.size(); }
  const std::vector<Displacement> &getDisplacements() const {
    return m_Displacements;
  }

  size_t getNormalCount() const { return m_Normals.size(); }
  const std::vector<Normal> &getNormals() const { return m_Normals; }

  // ---------------------------------------------------------------------
  // Setters

  void SetTileSize(uint32_t size);
  void SetTileLength(float length);

  /** @param w Unit vector - direction of wind blowing */
  void SetWindDirection(const glm::vec2 &w);

  /** @param v Positive floating point number - speed of the wind in its
   * direction */
  void SetWindSpeed(float v);

  void SetAnimationPeriod(float T);
  void SetPhillipsConst(float A);

  /** @param lambda Importance of displacement vector */
  void SetLambda(float lambda);

  /** @param Damping Suppresses wave lengths smaller that its value */
  void SetDamping(float damping);

private:
  using Complex = std::complex<float>;

  struct WaveVector {
    glm::vec2 vec;
    glm::vec2 unit;

    WaveVector(glm::vec2 v)
        : vec(v),
          unit(glm::length(v) > 0.00001f ? glm::normalize(v) : glm::vec2(0)) {}

    WaveVector(glm::vec2 v, glm::vec2 u) : vec(v), unit(u) {}
  };

  struct BaseWaveHeight {
    Complex heightAmp;      ///< FT amplitude of wave height
    Complex heightAmp_conj; ///< conjugate of wave height amplitude
    float dispersion;       ///< Descrete dispersion value
  };

  std::vector<WaveVector> ComputeWaveVectors() const;
  std::vector<Complex> ComputeGaussRandomArray() const;
  std::vector<BaseWaveHeight> ComputeBaseWaveHeightField(
      const std::vector<Complex> &gaussRandomArray) const;

  /**
   * @brief Normalizes heights to interval <-1, 1>
   * @return Ampltiude, to reconstruct the orignal signal
   */
  float NormalizeHeights(float minHeight, float maxHeight);

  void SetupFFTW();
  void DestroyFFTW();

private:
  // ---------------------------------------------------------------------
  // Properties

  uint32_t m_TileSize;
  float m_TileLength;

  glm::vec2 m_WindDir; ///< Unit vector
  float m_WindSpeed;

  // Phillips spectrum
  float m_A;
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

  std::vector<WaveVector> m_WaveVectors; ///< Precomputed Wave vectors

  // Base wave height field generated from the spectrum for each wave vector
  std::vector<BaseWaveHeight> m_BaseWaveHeights;

  // ---------------------------------------------------------------------
  // FT computation using FFTW

  Complex *m_Height{nullptr};
  Complex *m_SlopeX{nullptr};
  Complex *m_SlopeZ{nullptr};
  Complex *m_DisplacementX{nullptr};
  Complex *m_DisplacementZ{nullptr};
  Complex *m_dxDisplacementX{nullptr};
  Complex *m_dzDisplacementZ{nullptr};
#ifdef COMPUTE_JACOBIAN
  Complex *m_dxDisplacementZ{nullptr};
  Complex *m_dzDisplacementX{nullptr};
#endif

  fftwf_plan m_PlanHeight{nullptr};
  fftwf_plan m_PlanSlopeX{nullptr};
  fftwf_plan m_PlanSlopeZ{nullptr};
  fftwf_plan m_PlanDisplacementX{nullptr};
  fftwf_plan m_PlanDisplacementZ{nullptr};
  fftwf_plan m_PlandxDisplacementX{nullptr};
  fftwf_plan m_PlandzDisplacementZ{nullptr};
#ifdef COMPUTE_JACOBIAN
  fftwf_plan m_PlandxDisplacementZ{nullptr};
  fftwf_plan m_PlandzDisplacementX{nullptr};
#endif

  float m_MinHeight{-1.0f};
  float m_MaxHeight{1.0f};

private:
  static constexpr float s_kG{9.81}; ///< Gravitational constant
  static constexpr float s_kOneOver2sqrt{0.7071067812};

  /**
   * @brief Realization of water wave height field in fourier domain
   * @return Fourier amplitudes of a wave height field
   */
  Complex BaseWaveHeightFT(const Complex gaussRandom,
                           const glm::vec2 unitWaveVec, float k) const {
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

  static Complex WaveHeightFT(const BaseWaveHeight &waveHeight, const float t) {
    const float omega_t = waveHeight.dispersion * t;

    // exp(ix) = cos(x) * i*sin(x)
    const float pcos = glm::cos(omega_t);
    const float psin = glm::sin(omega_t);

    return waveHeight.heightAmp * Complex(pcos, psin) +
           waveHeight.heightAmp_conj * Complex(pcos, -psin);
  }

  // --------------------------------------------------------------------

  /**
   * @brief Computes quantization of the dispersion surface
   *  - of the frequencies of dispersion relation
   * @param k Magnitude of wave vector
   */
  inline constexpr float QDispersion(float k) const {
    return glm::floor(DispersionDeepWaves(k) / m_BaseFreq) * m_BaseFreq;
  }

  /**
   * @brief Dispersion relation for deep water, where the bottom may be
   *  ignored
   * @param k Magnitude of wave vector
   */
  inline constexpr float DispersionDeepWaves(float k) const {
    return glm::sqrt(s_kG * k);
  }

  /**
   * @brief Dispersion relation for waves where the bottom is relatively
   *  shallow compared to the length of the waves
   * @param k Magnitude of wave vector
   * @param D Depth below the mean water level
   */
  inline constexpr float DispersionTransWaves(float k, float D) const {
    return glm::sqrt(s_kG * k * glm::tanh(k * D));
  }

  /**
   * @brief Dispersion relation for very small waves, i.e., with
   *  wavelength of about >1 cm
   * @param k Magnitude of wave vector
   * @param L wavelength
   */
  inline constexpr float DispersionSmallWaves(float k, float L) const {
    return glm::sqrt(s_kG * k * (1 + k * k * L * L));
  }
};

} // namespace vkh
