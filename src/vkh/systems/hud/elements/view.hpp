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

  View(const View &) = delete;
  View &operator=(const View &) = delete;

  View(View &&other) noexcept
      : context(other.context), elements(std::move(other.elements)),
        elementCount(other.elementCount) {
    context.inputCallbackSystems[this] =
        std::move(context.inputCallbackSystems[&other]);
    context.inputCallbackSystems.erase(&other);
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
  auto size() const { return elements.size(); }
  auto empty() const { return elements.empty(); }

  ~View() {
    context.inputCallbackSystems.erase(this);
    elements.clear();
  }

  void setCurrent() { context.currentInputCallbackSystemKey = this; }

  EngineContext &context;
  std::vector<std::shared_ptr<Element>> elements;

  size_t elementCount = 0;

private:
  friend class Element;
  template <auto CallbackListPtr> friend class EventListener;
};

} // namespace hud
} // namespace vkh
