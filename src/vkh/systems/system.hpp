#pragma once

#include "../engineContext.hpp"
#include "../pipeline.hpp"

namespace vkh {
class System {
protected:
  System(EngineContext &context) : context{context} {}
  EngineContext &context;
};
} // namespace vkh
