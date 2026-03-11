#pragma once

#include "../../buffer.hpp"
#include "../system.hpp"

#include "fluidGrid.hpp"

namespace vkh {
class SmokeSys : public System {
public:
  SmokeSys(EngineContext &context);
  void update();
  void render();

  FluidGrid fluidGrid;

private:
  void createPipeline();

  std::unique_ptr<GraphicsPipeline> pipeline;
  std::unique_ptr<Buffer<float>> stagingBuffer;

  const unsigned int interpolatedScale = 4;
}; // namespace particlesSys
} // namespace vkh
