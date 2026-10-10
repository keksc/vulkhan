#include "assets.hpp"
#include "../vkh/systems/postProcessing.hpp"
#include "../vkh/systems/postprocessing/godRaysEffect.hpp"
#include "../vkh/systems/smoke/compositeEffect.hpp"
#include "../vkh/systems/smoke/smoke.hpp"
#include "../vkh/systems/tree/firGenerator.hpp"
#include "../vkh/systems/tree/rockGenerator.hpp"
#include "../vkh/systems/tree/treeSys.hpp"
namespace app {
void registerSceneryAssets(vkh::EngineContext &context, vkh::TreeSys &treeSys,
                           uint32_t seed) {
  {
    std::array<std::shared_ptr<vkh::Scene<vkh::TreeSys::Vertex>>, kTreeSpecies>
        high, low;
    for (uint32_t i = 0; i < kTreeSpecies; i++) {
      vkh::fir::Params fp;
      fp.seed = 1 + i;
      fp.height = 8.f + 0.6f * static_cast<float>(i);
      fp.broadleaf = i >= kFirSpecies;
      if (fp.broadleaf) {
        fp.height = 9.f + float(i - kFirSpecies) * .65f;
        fp.crownStart = .35f;
        fp.maxCrownRadius = 3.8f;
        fp.tiers = 14;
        fp.branchesBottom = 7;
        fp.branchesTop = 5;
        fp.trunkBaseRadius = .38f;
        fp.droopBottom = .08f;
        fp.liftBottom = .28f;
        fp.liftTop = .22f;
      }
      high[i] = vkh::fir::makeScene(context, treeSys.texturesSetLayout, fp,
                                    fp.broadleaf ? 512 : 1024);

      // Same seed/height => same silhouette, far fewer cards: 12 tiers
      // instead of 24, one card per branch instead of three fanned ones,
      // 2 segments per card instead of 4, wider cards to keep the crown
      // opaque enough, and a 512 atlas (UVs are size-independent).
      vkh::fir::Params lp = fp;
      lp.tiers = 12;
      lp.branchesBottom = 9;
      lp.branchesTop = 4;
      lp.fanCards = 1;
      lp.cardSegments = 2;
      lp.cardWidth = fp.cardWidth * 1.6f;
      lp.trunkSides = 5;
      lp.trunkRings = 3;
      low[i] = vkh::fir::makeScene(context, treeSys.texturesSetLayout, lp,
                                   /*atlasSize=*/512);
    }
    auto registerTier = [&](auto &scenes, uint32_t passMask) {
      for (uint32_t i = 0; i < kTreeSpecies; i++)
        treeSys.addVariant({scenes[i], {false, true}, passMask});
    };
    registerTier(high, vkh::TreeSys::ePassMain);      // kTierHigh
    registerTier(low, vkh::TreeSys::ePassAll);        // kTierLowBoth
    registerTier(low, vkh::TreeSys::ePassReflection); // kTierLowRefl
    registerTier(low, vkh::TreeSys::ePassMain);       // kTierLowMain
  }

  for (uint32_t i = 0; i < kRockSpecies; ++i)
    treeSys.addVariant(
        {vkh::rocks::makeScene(context, treeSys.texturesSetLayout,
                               seed + i * 977u),
         {false},
         vkh::TreeSys::ePassAll,
         vkh::TreeSys::eRock});
}
std::unique_ptr<vkh::PostProcessingSys>
createPostProcessing(vkh::EngineContext &context, vkh::SmokeSys &smokeSys) {
  std::vector<vkh::ComputePassFactory> postEffects;
  postEffects.push_back(vkh::makeComputePassFactory<vkh::SmokeCompositePush>(
      [&smokeSys](vkh::EngineContext &ctx) {
        return std::make_unique<vkh::SmokeCompositeEffect>(ctx, smokeSys);
      }));
  postEffects.push_back(vkh::makeComputePassFactory<vkh::BlackHoleGodRaysPush>(
      [](vkh::EngineContext &ctx) {
        auto effect = std::make_unique<vkh::GodRaysEffect>(ctx);
        return effect;
      }));
  return std::make_unique<vkh::PostProcessingSys>(context,
                                                  std::move(postEffects));
}
} // namespace app
