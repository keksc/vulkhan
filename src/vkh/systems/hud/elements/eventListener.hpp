#pragma once

#include "view.hpp"

namespace vkh {
namespace hud {
template <auto CallbackListPtr> class EventListener {
public:
  EventListener(
      View &view,
      std::decay_t<decltype(View::context.inputCallbackSystems[&view].*
                            CallbackListPtr)>::mapped_type callback)
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
