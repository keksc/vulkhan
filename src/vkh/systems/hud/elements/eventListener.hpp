#pragma once

#include "view.hpp"

namespace vkh {
namespace hud {
template <auto CallbackListPtr> class EventListener {
public:
  using CallbackType = typename std::decay_t<
      decltype(std::declval<View>().context.inputCallbackSystems[nullptr].*
               CallbackListPtr)>::mapped_type;
  EventListener(
      View &view,
      CallbackType callback)
      : parentView{view} {
    auto &callbackList =
        parentView.context.inputCallbackSystems[&parentView].*CallbackListPtr;
    callbackList[this] = callback;
  }
  ~EventListener() {
    auto &callbackList =
        parentView.context.inputCallbackSystems[&parentView].*CallbackListPtr;
    callbackList.erase(this);
  }

private:
  View &parentView;
};
} // namespace hud
} // namespace vkh
