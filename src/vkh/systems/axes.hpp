#pragma once

#include "system.hpp"

namespace vkh {
class AxesSys : public System {
public:
  AxesSys(EngineContext &context);

  void render();

private:
  void createPipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;
};
} // namespace vkh
