#pragma once

#include <glm/glm.hpp>
#include <vkFFT.h>
#include <vulkan/vulkan.hpp>

#include "../system.hpp"

#include <bit>
#include <complex>
#include <memory>
#include <vector>

namespace vkh {

template <typename T> class Buffer;
class ComputePipeline;
class EngineContext;
class Image2D;

class WSTessendorf : public System {
public:
  WSTessendorf(EngineContext &context, float patchLength = tileLength);
  ~WSTessendorf();

  void recordComputeWaves(vk::CommandBuffer &cmd, float time);

  // One-time warm-up dispatch of this sim's compute pipeline, run on its
  // own single-use command buffer via computeQueue. Exists because the
  // FIRST recordComputeWaves() call after construction was otherwise
  // deferred until whichever WaterSys instance happens to run first calls
  // update() - which used to mean callers needing every WaterSys sharing
  // this sim to see rendered waves at simulation-derived time == 0 rather
  // than a stale (possibly quite advanced) call to
  // static_cast<float>(glfwGetTime()) had to explicitly reach through
  // some arbitrary WaterSys instance (see the removed WaterSys::prepare()
  // - it existed for exactly this, and required callers to know they only
  // needed to call it on ONE lake out of however many shared this tess,
  // which was easy to get wrong and had nothing to do with any particular
  // lake). Call this once, right after constructing the WSTessendorf,
  // regardless of how many (or how few - including zero, for a level with
  // no lakes) WaterSys instances end up sharing it.
  void warmUp();

  static constexpr unsigned int tileSize = 512; // has to be a power of 2
  static constexpr unsigned int tileSizeSquared = tileSize * tileSize;
  static constexpr float tileLength = 1000.f;
  static constexpr float detailTileLength = 8.f;
  // Full mip chain down to 1x1: log2(512) + 1 = 10 levels. Regenerated
  // every frame in recordGenerateMips() since displacementFoamMap's mip 0
  // is rewritten every frame by postFFT.comp - this isn't a load-time
  // mip chain, it has to be rebuilt each time the underlying data changes.
  static constexpr unsigned int mipLevelCount =
      static_cast<unsigned int>(std::bit_width(tileSize));

  Image2D &getDisplacementFoamImage() { return *displacementFoamMap; }
  Image2D &getDetailImage() {
    return detailCascade ? detailCascade->getDisplacementFoamImage()
                         : *displacementFoamMap;
  }

private:
  float patchLength;
  std::unique_ptr<WSTessendorf> detailCascade;
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

  vk::DescriptorSetLayout preFFTSetLayout;
  vk::DescriptorSet preFFTSet;
  vk::DescriptorSetLayout postFFTSetLayout;
  vk::DescriptorSet postFFTSet;

  VkFFTApplication app{};
  vk::Fence fence;

  VkPhysicalDevice vkfftPhysicalDevice{};
  VkDevice vkfftDevice{};
  VkQueue vkfftComputeQueue{};
  VkCommandPool vkfftCommandPool{};
  VkFence vkfftFence{};
  VkBuffer vkfftBuffer{};
  uint64_t vkfftBufferSize{};

  std::unique_ptr<ComputePipeline> preFFTPipeline;
  std::unique_ptr<ComputePipeline> postFFTPipeline;
  std::unique_ptr<ComputePipeline> baseWaveHeightFieldPipeline;

  struct PushConstantData {
    float t;
    float dt;
  };

  struct WindPushConstantData {
    glm::vec2 windDir;
    float windSpeed; // m/s - physical
    float _pad{0.f};
  };

  // Last wind values the spectrum was actually (re)built for - compared
  // against the engine's live global wind every frame in
  // recordUpdateSpectrumIfWindChanged() so that shader only re-dispatches
  // (and pays for integrating the JONSWAP spectrum over all
  // tileSizeSquared wave vectors again) when wind has genuinely changed,
  // not every single frame. Sentinel strength of -1 guarantees the very
  // first call always dispatches, since getGlobalWindStrength() is
  // clamped to [0, 1] and can never equal it.
  glm::vec2 lastSpectrumWindDir{1.f, 0.f};
  float lastSpectrumWindStrength{-1.f};

  void createDescriptors();

  // Rebuilds baseWaveHeightField from the engine's CURRENT global wind
  // (see water.hpp's setGlobalWindDirection()/setGlobalWindStrength()) if
  // it's changed since the last time this ran - a no-op most frames.
  // Must run (and its dispatch complete, via the barrier it records)
  // before preFFT.comp reads baseWaveHeightField each frame - called from
  // the top of recordComputeWaves() for exactly that reason.
  void recordUpdateSpectrumIfWindChanged(vk::CommandBuffer &cmd);

  // Blits displacementFoamMap's mip 0 (just written by postFFT.comp) down
  // through mipLevelCount - 1 progressively half-sized levels, so the
  // water sampler (see WaterSys::createSampler()'s eRepeat/eLinear mip
  // setup) has an actual chain to filter across instead of aliasing the
  // full-res wave detail at any distance/grazing angle. Called once per
  // frame from recordComputeWaves(), right after the post-FFT dispatch.
  void recordGenerateMips(vk::CommandBuffer &cmd);

  std::unique_ptr<Image2D> displacementFoamMap;
};

} // namespace vkh
