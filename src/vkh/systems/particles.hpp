#pragma once

#include "system.hpp"

namespace vkh {
class ParticleSys : public System {
public:
  ParticleSys(EngineContext &context);
  void render();

private:
  void createPipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;
}; // namespace particlesSys
} // namespace vkh
