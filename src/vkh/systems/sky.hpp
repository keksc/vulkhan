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

  // This system should be among the first to be rendered
  void render();

private:
  std::unique_ptr<GraphicsPipeline> pipeline;
};

} // namespace vkh
