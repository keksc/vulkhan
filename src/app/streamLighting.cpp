#include "streamLighting.hpp"
#include "../vkh/engineContext.hpp"
#include "../vkh/streamLightCache.hpp"
#include "../vkh/systems/terrain/terrain.hpp"
namespace app {
struct StreamLighting::Impl {
  vkh::EngineContext &context;
  vkh::TerrainSys &terrain;
  std::function<bool(glm::vec2)> blocked;
  vkh::StreamLightCache cache;
};
StreamLighting::StreamLighting(vkh::EngineContext &context,
                               vkh::TerrainSys &terrain,
                               std::function<bool(glm::vec2)> blocked)
    : impl(std::make_unique<Impl>(context, terrain, std::move(blocked))) {}
StreamLighting::~StreamLighting() = default;
void StreamLighting::update() {
  auto &state = *impl;
  state.context.environment.lights = state.cache.update(
      state.context.camera.position,
      [&](glm::vec2 p) { return state.terrain.sampleHeight(p, false); },
      state.blocked, state.context.time);
}
} // namespace app
