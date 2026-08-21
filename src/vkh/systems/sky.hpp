#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "system.hpp"
#include <memory>
#include <vector>

namespace vkh {
class Image2D;
class Cubemap;
class GraphicsPipeline;
template <typename T> class Scene;

class SkySys : public System {
public:
  // useCachedBake controls whether bakeEnvironment() is allowed to load a
  // previously-baked environment from disk instead of recomputing it on
  // the GPU (see sky.cpp). SkySys itself has no notion of user settings --
  // it's graphics-layer code -- so it's on the caller to decide this (e.g.
  // by reading vkh::settings::current().useCachedSkyBake in main.cpp) and
  // pass the plain bool in.
  //
  // milkyWayFaceSize/discTurbulenceSize control the bake resolution. These
  // used to be compile-time constants; they're now constructor parameters
  // (and can change later via rebake()) so the resolution can be a user
  // setting. tryLoadBakedFromCache() validates any on-disk cache against
  // these values (via file size and a resolution-tagged filename), so
  // changing resolution automatically invalidates a stale cache rather
  // than silently loading mismatched data.
  SkySys(EngineContext &context, uint32_t milkyWayFaceSize = 512,
         uint32_t discTurbulenceSize = 256, bool useCachedBake = true);
  ~SkySys();

  // This system should be among the first to be rendered, depth testing
  // disabled
  void render();

  // Re-bakes at a new resolution, live, replacing the currently rendered
  // environment. Blocks on vkDeviceWaitIdle first since the GPU may still
  // be reading milkyWayCubemap/discTurbulence/bakedSet from an in-flight
  // frame -- safe to call from anywhere (e.g. a UI button callback) but
  // not cheap, so only call it on an actual user-initiated change, not
  // per-frame. No-op if the requested resolution already matches the
  // current one.
  void rebake(uint32_t newMilkyWayFaceSize, uint32_t newDiscTurbulenceSize);

  uint32_t getMilkyWayFaceSize() const { return milkyWayFaceSize; }
  uint32_t getDiscTurbulenceSize() const { return discTurbulenceSize; }

private:
  void bakeEnvironment();
  void createBakedDescriptorSetLayout();

  // Persistent-storage cache for bakeEnvironment()'s output, so the compute
  // bake only has to run once ever per resolution, not once per launch.
  // See sky.cpp.
  //
  // Returns true and leaves milkyWayCubemap/discTurbulence fully populated
  // and in eShaderReadOnlyOptimal layout if a valid cache was found and
  // loaded; returns false (images left untouched/null) if there was no
  // usable cache or useCachedBake is false, in which case
  // bakeEnvironment() falls back to the normal compute-shader bake.
  bool tryLoadBakedFromCache();

  // Downloads the current (freshly baked) milkyWayCubemap/discTurbulence
  // contents to disk so tryLoadBakedFromCache() can pick them up next
  // launch. Assumes both images currently hold their final baked contents;
  // leaves their layout unchanged (Image::downloadPixels restores it).
  void saveBakedToCache();

  // Rewrites bakedSet's two image bindings to point at the current
  // milkyWayCubemap/discTurbulence. Shared by the constructor and
  // rebake() so both stay in sync.
  void writeBakedDescriptorSet();

  bool useCachedBake;
  uint32_t milkyWayFaceSize;
  uint32_t discTurbulenceSize;

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::unique_ptr<Cubemap> milkyWayCubemap;
  std::unique_ptr<Image2D> discTurbulence;
  vk::DescriptorSetLayout bakedSetLayout;
  vk::DescriptorSet bakedSet;
};

} // namespace vkh
