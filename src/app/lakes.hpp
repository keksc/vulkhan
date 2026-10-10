#pragma once
#include "../mountain.hpp"
#include <array>
#include <list>
#include <memory>
namespace vkh {
class SkySys;
class WSTessendorf;
class TerrainSys;
class WaterSys;
class PlanarReflectionSys;
namespace trees {
class ExclusionZones;
}
} // namespace vkh
namespace app {
struct ActiveLake {
  vkh::mountain::Lake lake;
  std::unique_ptr<vkh::WaterSys> water;
  std::pair<int32_t, int32_t> tile;
  size_t index;
  size_t group;
  bool visible{true};      // in frustum and not hidden by terrain
  bool occluded{false};    // last terrain-occlusion result
  uint64_t testedFrame{0}; // lakeFrame of that result (0 = never)
};
struct ReflectionGroup {
  float planeY{};
  uint64_t lastRefreshFrame{0}; // lakeFrame of its last reflection pass
  std::unique_ptr<vkh::PlanarReflectionSys> refl;
  std::vector<ActiveLake *> lakes;
};

void configureInitialLakeZones(vkh::TerrainSys &,
                               const std::vector<vkh::mountain::Lake> &);
class LakeStreaming {
public:
  LakeStreaming(vkh::EngineContext &, vkh::SkySys &, vkh::WSTessendorf &,
                vkh::TerrainSys &, const vkh::mountain::MountainCreateInfo &,
                std::vector<vkh::mountain::Lake>);
  ~LakeStreaming();
  void update(const glm::vec3 &, size_t budget);
  void updateVisibility(const std::array<glm::vec4, 6> &, const glm::vec3 &);
  vkh::trees::ExclusionZones &zones();
  std::list<ActiveLake> &active();
  std::vector<std::unique_ptr<ReflectionGroup>> &groups();
  uint64_t frame() const;
  float distance(const glm::vec2 &, const vkh::mountain::Lake &) const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};
} // namespace app
