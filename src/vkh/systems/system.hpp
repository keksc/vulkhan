#pragma once

#include "../engineContext.hpp"
#include "../pipeline.hpp"

namespace vkh {
class System {
  System(EngineContext &context) : context{context} {}

private:
  EngineContext &context;
};
} // namespace vkh
