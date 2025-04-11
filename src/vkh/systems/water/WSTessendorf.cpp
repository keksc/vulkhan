#include "WSTessendorf.hpp"

#include <algorithm>

namespace vkh {
void WSTessendorf::ifft1d(std::complex<float> *data, int N) {
  const float pi = 3.14159265358979323846f;
  int j = 0;
  for (int i = 0; i < N - 1; ++i) {
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
    for (int m = 0; m < mmax; ++m) {
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
void WSTessendorf::ifft2d(std::complex<float> *data, int N) {
  // Process rows
  for (int i = 0; i < N; ++i)
    ifft1d(data + i * N, N);

  // Process columns
  std::vector<std::complex<float>> col(N);
  for (int j = 0; j < N; ++j) {
    for (int i = 0; i < N; ++i)
      col[i] = data[i * N + j];
    ifft1d(col.data(), N);
    for (int i = 0; i < N; ++i)
      data[i * N + j] = col[i];
  }
}
WSTessendorf::WSTessendorf(uint32_t tileSize, float tileLength) {
  SetTileSize(tileSize);
  SetTileLength(tileLength);

  SetWindDirection(defaultWindDir);
  SetWindSpeed(defaultWindSpeed);
  SetAnimationPeriod(defaultAnimPeriod);

  SetPhillipsConst(defaultPhillipsConst);
  SetDamping(defaultPhillipsDamping);
}

WSTessendorf::~WSTessendorf() {
  DestroyFFTW();
}

void WSTessendorf::Prepare() {
  m_WaveVectors = ComputeWaveVectors();

  std::vector<Complex> gaussRandomArr = ComputeGaussRandomArray();
  m_BaseWaveHeights = ComputeBaseWaveHeightField(gaussRandomArr);

  const uint32_t kSize = m_TileSize;

  const Displacement kDefaultDisplacement{0.0};
  m_Displacements.resize(kSize * kSize, kDefaultDisplacement);

  const Normal kDefaultNormal{0.0, 1.0, 0.0, 0.0};
  m_Normals.resize(kSize * kSize, kDefaultNormal);

  DestroyFFTW();
  SetupFFTW();
}

std::vector<WSTessendorf::WaveVector> WSTessendorf::ComputeWaveVectors() const {
  const int32_t kSize = m_TileSize;
  const float kLength = m_TileLength;

  std::vector<WaveVector> waveVecs;
  waveVecs.reserve(kSize * kSize);

  for (int32_t m = 0; m < kSize; ++m) {
    for (int32_t n = 0; n < kSize; ++n) {
      waveVecs.emplace_back(glm::vec2(M_PI * (2.0f * n - kSize) / kLength,
                                      M_PI * (2.0f * m - kSize) / kLength));
    }
  }

  return waveVecs;
}

std::vector<WSTessendorf::Complex>
WSTessendorf::ComputeGaussRandomArray() const {
  const uint32_t kSize = m_TileSize;
  std::vector<Complex> randomArr(kSize * kSize);

  for (int32_t m = 0; m < kSize; ++m)
    for (int32_t n = 0; n < kSize; ++n) {
      randomArr[m * kSize + n] =
          Complex(glm::gaussRand(0.0f, 1.0f), glm::gaussRand(0.0f, 1.0f));
    }

  return randomArr;
}

std::vector<WSTessendorf::BaseWaveHeight>
WSTessendorf::ComputeBaseWaveHeightField(
    const std::vector<Complex> &gaussRandomArray) const {
  const uint32_t kSize = m_TileSize;

  std::vector<WSTessendorf::BaseWaveHeight> baseWaveHeights(kSize * kSize);
  assert(m_WaveVectors.size() == baseWaveHeights.size());
  assert(baseWaveHeights.size() == gaussRandomArray.size());

#pragma omp parallel for collapse(2) schedule(guided)
  for (uint32_t m = 0; m < kSize; ++m) {
    for (uint32_t n = 0; n < kSize; ++n) {
      const uint32_t kIndex = m * kSize + n;
      const auto &kWaveVec = m_WaveVectors[kIndex];
      const float k = glm::length(kWaveVec.vec);

      auto &h0 = baseWaveHeights[kIndex];
      if (k > 0.00001f) {
        const auto gaussRandom = gaussRandomArray[kIndex];
        h0.heightAmp = BaseWaveHeightFT(gaussRandom, kWaveVec.unit, k);
        h0.heightAmp_conj =
            std::conj(BaseWaveHeightFT(gaussRandom, -kWaveVec.unit, k));
        h0.dispersion = QDispersion(k);
      } else {
        h0.heightAmp = Complex(0);
        h0.heightAmp_conj = std::conj(Complex(0));
        h0.dispersion = 0.0f;
      }
    }
  }

  return baseWaveHeights;
}

void WSTessendorf::SetupFFTW() {
  const uint32_t kSize = m_TileSize;
  const uint32_t kSize2 = kSize * kSize;

#ifndef CAREFUL_ALLOC
#ifndef COMPUTE_JACOBIAN
  const uint32_t kTotalInputs = 7;
#else
  const uint32_t kTotalInputs = 9;
#endif
  Complex *buffer = new Complex[kTotalInputs * kSize2];
  m_Height = buffer;
  m_SlopeX = buffer + kSize2;
  m_SlopeZ = m_SlopeX + kSize2;
  m_DisplacementX = m_SlopeZ + kSize2;
  m_DisplacementZ = m_DisplacementX + kSize2;
  m_dxDisplacementX = m_DisplacementZ + kSize2;
  m_dzDisplacementZ = m_dxDisplacementX + kSize2;
#ifdef COMPUTE_JACOBIAN
  m_dzDisplacementX = m_dzDisplacementZ + kSize2;
  m_dxDisplacementZ = m_dzDisplacementX + kSize2;
#endif
#else
  m_Height = new Complex[kSize2];
  m_SlopeX = new Complex[kSize2];
  m_SlopeZ = new Complex[kSize2];
  m_DisplacementX = new Complex[kSize2];
  m_DisplacementZ = new Complex[kSize2];
  m_dxDisplacementX = new Complex[kSize2];
  m_dzDisplacementZ = new Complex[kSize2];
#ifdef COMPUTE_JACOBIAN
  m_dzDisplacementX = new Complex[kSize2];
  m_dxDisplacementZ = new Complex[kSize2];
#endif
#endif
}

void WSTessendorf::DestroyFFTW() {
  if (m_Height == nullptr)
    return;

#ifndef CAREFUL_ALLOC
  delete[] m_Height;
#else
  delete[] m_Height;
  delete[] m_SlopeX;
  delete[] m_SlopeZ;
  delete[] m_DisplacementX;
  delete[] m_DisplacementZ;
  delete[] m_dxDisplacementX;
  delete[] m_dzDisplacementZ;
#ifdef COMPUTE_JACOBIAN
  delete[] m_dxDisplacementZ;
  delete[] m_dzDisplacementX;
#endif
#endif

  m_Height = nullptr;
  m_SlopeX = nullptr;
  m_SlopeZ = nullptr;
  m_DisplacementX = nullptr;
  m_DisplacementZ = nullptr;
  m_dxDisplacementX = nullptr;
  m_dzDisplacementZ = nullptr;
#ifdef COMPUTE_JACOBIAN
  m_dxDisplacementZ = nullptr;
  m_dzDisplacementX = nullptr;
#endif
}

float WSTessendorf::computeWaves(float t) {
  const auto kTileSize = m_TileSize;

  float masterMaxHeight = std::numeric_limits<float>::min();
  float masterMinHeight = std::numeric_limits<float>::max();

#pragma omp parallel shared(masterMaxHeight, masterMinHeight)
  {
#pragma omp for collapse(2) schedule(static)
    for (uint32_t m = 0; m < kTileSize; ++m)
      for (uint32_t n = 0; n < kTileSize; ++n) {
        m_Height[m * kTileSize + n] =
            WaveHeightFT(m_BaseWaveHeights[m * kTileSize + n], t);
      }

// Slopes for normals computation
#pragma omp for collapse(2) schedule(static) nowait
    for (uint32_t m = 0; m < kTileSize; ++m)
      for (uint32_t n = 0; n < kTileSize; ++n) {
        const uint32_t kIndex = m * kTileSize + n;

        const auto &kWaveVec = m_WaveVectors[kIndex].vec;
        m_SlopeX[kIndex] = Complex(0, kWaveVec.x) * m_Height[kIndex];
        m_SlopeZ[kIndex] = Complex(0, kWaveVec.y) * m_Height[kIndex];
      }

// Displacement vectors
#pragma omp for collapse(2) schedule(static)
    for (uint32_t m = 0; m < kTileSize; ++m)
      for (uint32_t n = 0; n < kTileSize; ++n) {
        const uint32_t kIndex = m * kTileSize + n;

        const auto &kWaveVec = m_WaveVectors[kIndex];
        m_DisplacementX[kIndex] =
            Complex(0, -kWaveVec.unit.x) * m_Height[kIndex];
        m_DisplacementZ[kIndex] =
            Complex(0, -kWaveVec.unit.y) * m_Height[kIndex];
        m_dxDisplacementX[kIndex] =
            Complex(0, kWaveVec.vec.x) * m_DisplacementX[kIndex];
        m_dzDisplacementZ[kIndex] =
            Complex(0, kWaveVec.vec.y) * m_DisplacementZ[kIndex];
#ifdef COMPUTE_JACOBIAN
        m_dzDisplacementX[kIndex] =
            Complex(0, kWaveVec.vec.y) * m_DisplacementX[kIndex];
        m_dxDisplacementZ[kIndex] =
            Complex(0, kWaveVec.vec.x) * m_DisplacementZ[kIndex];
#endif
      }

#pragma omp sections
    {
#pragma omp section
      {
        ifft2d(m_Height, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_SlopeX, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_SlopeZ, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_DisplacementX, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_DisplacementZ, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_dxDisplacementX, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_dzDisplacementZ, m_TileSize);
      }
#ifdef COMPUTE_JACOBIAN
#pragma omp section
      {
        ifft2d(m_dzDisplacementX, m_TileSize);
      }
#pragma omp section
      {
        ifft2d(m_dxDisplacementZ, m_TileSize);
      }
#endif
    }

    float maxHeight = std::numeric_limits<float>::min();
    float minHeight = std::numeric_limits<float>::max();

    // Conversion of the grid back to interval
    //  [-m_TileSize/2, ..., 0, ..., m_TileSize/2]
    const float kSigns[] = {1.0f, -1.0f};

#pragma omp for collapse(2) schedule(static) nowait
    for (uint32_t m = 0; m < kTileSize; ++m) {
      for (uint32_t n = 0; n < kTileSize; ++n) {
        const uint32_t kIndex = m * kTileSize + n;
        const int sign = kSigns[(n + m) & 1];
        const auto h_FT = m_Height[kIndex].real() * static_cast<float>(sign);
        maxHeight = glm::max(h_FT, maxHeight);
        minHeight = glm::min(h_FT, minHeight);

        auto &displacement = m_Displacements[kIndex];
        displacement.y = h_FT;
        displacement.x = static_cast<float>(sign) * m_Lambda *
                         m_DisplacementX[kIndex].real();
        displacement.z = static_cast<float>(sign) * m_Lambda *
                         m_DisplacementZ[kIndex].real();
        displacement.w = 1.0f;
      }
    }
// TODO reduction
#pragma omp critical
    {
      masterMaxHeight = glm::max(maxHeight, masterMaxHeight);
      masterMinHeight = glm::min(minHeight, masterMinHeight);
    }

#pragma omp for collapse(2) schedule(static) nowait
    for (uint32_t m = 0; m < kTileSize; ++m) {
      for (uint32_t n = 0; n < kTileSize; ++n) {
        const uint32_t kIndex = m * kTileSize + n;
        const int sign = kSigns[(n + m) & 1];
#ifdef COMPUTE_JACOBIAN
        const float jacobian =
            (1.0f + m_Lambda * sign * m_dxDisplacementX[kIndex].real()) *
                (1.0f + m_Lambda * sign * m_dzDisplacementZ[kIndex].real()) -
            (m_Lambda * sign * m_dxDisplacementZ[kIndex].real()) *
                (m_Lambda * sign * m_dzDisplacementX[kIndex].real());
        displacement.w = jacobian;
#endif

        m_Normals[kIndex] = glm::vec4(sign * m_SlopeX[kIndex].real(),
                                      sign * m_SlopeZ[kIndex].real(),
                                      sign * m_dxDisplacementX[kIndex].real(),
                                      sign * m_dzDisplacementZ[kIndex].real());
      }
    }
  }

  return NormalizeHeights(masterMinHeight, masterMaxHeight);
}

float WSTessendorf::NormalizeHeights(float minHeight, float maxHeight) {
  m_MinHeight = minHeight;
  m_MaxHeight = maxHeight;

  const float A = glm::max(glm::abs(minHeight), glm::abs(maxHeight));
  const float OneOverA = 1.f / A;

  std::for_each(m_Displacements.begin(), m_Displacements.end(),
                [OneOverA](auto &d) { d.y *= OneOverA; });

  return A;
}

// =============================================================================

void WSTessendorf::SetTileSize(uint32_t size) {
  const bool kSizeIsPowerOfTwo = (size & (size - 1)) == 0;
  assert(size > 0 && kSizeIsPowerOfTwo && "Tile size must be power of two");
  if (!kSizeIsPowerOfTwo)
    return;

  m_TileSize = size;
}

void WSTessendorf::SetTileLength(float length) {
  assert(length > 0.0);
  m_TileLength = length;
}

void WSTessendorf::SetWindDirection(const glm::vec2 &w) {
  m_WindDir = glm::normalize(w);
}

void WSTessendorf::SetWindSpeed(float v) { m_WindSpeed = glm::max(0.0001f, v); }

void WSTessendorf::SetAnimationPeriod(float T) {
  m_AnimationPeriod = T;
  m_BaseFreq = 2.0f * M_PI / m_AnimationPeriod;
}

void WSTessendorf::SetPhillipsConst(float A) { m_A = A; }

void WSTessendorf::SetLambda(float lambda) { m_Lambda = lambda; }

void WSTessendorf::SetDamping(float damping) { m_Damping = damping; }

} // namespace vkh
