#pragma once

#include <glm/glm.hpp>
#include <vulkan/vulkan.hpp>

#include "../image.hpp"
#include "system.hpp"
#include <memory>
#include <vector>

namespace vkh {

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
  SkySys(EngineContext &context, bool useCachedBake = true);
  ~SkySys();

  // This system should be among the first to be rendered, depth testing
  // disabled
  void render();

private:
  void bakeEnvironment();
  void createBakedDescriptorSetLayout();

  // Persistent-storage cache for bakeEnvironment()'s output, so the compute
  // bake only has to run once ever, not once per launch. See sky.cpp.
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

  bool useCachedBake;

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::unique_ptr<Image> milkyWayCubemap;
  std::unique_ptr<Image> discTurbulence;
  vk::DescriptorSetLayout bakedSetLayout;
  vk::DescriptorSet bakedSet;

  static constexpr uint32_t MILKY_WAY_FACE_SIZE = 512;
  static constexpr uint32_t DISC_TURBULENCE_SIZE = 256;
};

} // namespace vkh
