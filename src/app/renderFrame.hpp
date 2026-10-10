#pragma once
class GameUI;
namespace vkh {
class EngineContext;
class TerrainSys;
class EntitySys;
class WolfSys;
class TreeSys;
class ProceduralGrassSys;
class SkySys;
class ParticleSys;
class PostProcessingSys;
class GpuProfiler;
} // namespace vkh
namespace app {
class LakeStreaming;
struct RenderSystems {
  GameUI &ui;
  vkh::TerrainSys &terrainSys;
  vkh::EntitySys &entitySys;
  vkh::WolfSys &wolfSys;
  vkh::TreeSys &treeSys;
  vkh::ProceduralGrassSys &grassSys;
  vkh::SkySys &skySys;
  vkh::ParticleSys &particleSys;
  vkh::PostProcessingSys &postProcessingSys;
  vkh::GpuProfiler &gpuProfiler;
};
void renderFrame(vkh::EngineContext &, RenderSystems, LakeStreaming &);
} // namespace app
