#pragma once

#include "element.hpp"

namespace vkh {
class HudSys;
class EngineContext;
namespace hud {
class View {
public:
  View(EngineContext &context, HudSys &hudSys);
  View(const View &) = delete;
  View &operator=(const View &) = delete;

  View(View &&other) = delete;

  ~View();

  void setCurrent();

  EngineContext &context;
  Element container;

  size_t elementCount = 0;

  HudSys &hudSys;

private:
  friend class Element;
  template <auto CallbackListPtr> friend class EventListener;
};

} // namespace hud
} // namespace vkh
