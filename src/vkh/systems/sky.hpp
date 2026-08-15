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
  SkySys(EngineContext &context);
  ~SkySys();

  // This system should be among the first to be rendered, depth testing disabled
  void render();

private:
  void bakeEnvironment();
  void createBakedDescriptorSetLayout();

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::unique_ptr<Image> milkyWayCubemap;
  std::unique_ptr<Image> discTurbulence;
  vk::DescriptorSetLayout bakedSetLayout;
  vk::DescriptorSet bakedSet;

  static constexpr uint32_t MILKY_WAY_FACE_SIZE = 512;
  static constexpr uint32_t DISC_TURBULENCE_SIZE = 256;
};

} // namespace vkh
