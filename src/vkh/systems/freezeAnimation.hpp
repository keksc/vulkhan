#pragma once

#include "system.hpp"

namespace vkh {
class FreezeAnimationSys : public System {
public:
  FreezeAnimationSys(EngineContext &context);

  void render();

private:
  void createPipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;
};
} // namespace vkh
