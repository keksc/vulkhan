#pragma once
#include <cstdint>
#include <memory>
namespace vkh {
class EngineContext;
class TreeSys;
class SmokeSys;
class PostProcessingSys;
} // namespace vkh
namespace app {
inline constexpr uint32_t kFirSpecies = 6, kTreeSpecies = 12, kRockSpecies = 6,
                          kRockVariantBase = 4 * kTreeSpecies;
enum TreeTier : uint32_t {
  kTierHigh,
  kTierLowBoth,
  kTierLowRefl,
  kTierLowMain
};
void registerSceneryAssets(vkh::EngineContext &, vkh::TreeSys &, uint32_t seed);
std::unique_ptr<vkh::PostProcessingSys>
createPostProcessing(vkh::EngineContext &, vkh::SmokeSys &);
} // namespace app
