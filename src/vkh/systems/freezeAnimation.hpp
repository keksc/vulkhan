#pragma once

#include "system.hpp"
#include <memory>
#include "../pipeline.hpp"

namespace vkh {
class GraphicsPipeline;
class FreezeAnimationSys : public System {
public:
  FreezeAnimationSys(EngineContext &context);

  void render();

private:
  void createPipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;
};
} // namespace vkh
