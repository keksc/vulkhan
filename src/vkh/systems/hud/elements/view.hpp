#pragma once

#include "../../../engineContext.hpp"

namespace vkh {
namespace hud {
class Element;
class View {
public:
  View(EngineContext &context) : context{context} {
    context.inputCallbackSystems[this] = {};
  }

  template <typename T, typename... Args>
  std::shared_ptr<T> addElement(Args &&...args) {
    auto element =
        std::make_shared<T>(*this, nullptr, std::forward<Args>(args)...);
    elements.emplace_back(element);
    return element;
  }

  auto begin() { return elements.begin(); }
  auto end() { return elements.end(); }
  auto begin() const { return elements.begin(); }
  auto end() const { return elements.end(); }

  ~View() {
    context.inputCallbackSystems.erase(this);
    elements.clear();
  }

  void setCurrent() { context.currentInputCallbackSystemKey = this; }

  EngineContext &context;

private:
  std::vector<std::shared_ptr<Element>> elements;
  friend class Element;
  template <auto CallbackListPtr> friend class EventListener;
};

} // namespace hud
} // namespace vkh
