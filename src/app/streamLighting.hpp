#pragma once
#include <functional>
#include <glm/glm.hpp>
#include <memory>
namespace vkh {
class EngineContext;
class TerrainSys;
} // namespace vkh
namespace app {
class StreamLighting {
public:
  StreamLighting(vkh::EngineContext &, vkh::TerrainSys &,
                 std::function<bool(glm::vec2)> blocked);
  ~StreamLighting();
  void update();

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace app
