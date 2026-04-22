#pragma once

namespace vkh {
class EngineContext;
class System {
protected:
  System(EngineContext &context) : context{context} {}
  EngineContext &context;
};
} // namespace vkh
