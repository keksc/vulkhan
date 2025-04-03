#pragma once

#include <complex>
#include <fmt/format.h>
#include <functional>
#include <glm/glm.hpp>

#include <memory>
#include <type_traits>
#include <utility>

#include "../engineContext.hpp"

namespace vkh {
namespace hudSys {
struct Vertex {
  glm::vec2 position{};
  glm::vec3 color{};
};
struct DrawInfo {
  std::vector<Vertex> vertices;
  std::vector<uint32_t> indices;
};
class Element {
public:
  virtual ~Element() = default;
  virtual void updateDrawInfo(uint32_t baseIndex) = 0;
  DrawInfo drawInfo{};
  virtual void addChild(std::shared_ptr<Element> element) = 0;

protected:
  std::vector<std::shared_ptr<Element>> children;
  friend void addElementToDraw(std::shared_ptr<Element> element);
};

class Rect : public Element {
public:
  Rect(glm::vec2 position, glm::vec2 size, glm::vec3 color)
      : position{position}, size{size}, color{color} {};
  glm::vec2 position{};
  glm::vec2 size{};
  glm::vec3 color{};
  void updateDrawInfo(uint32_t baseIndex) override {
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    drawInfo.vertices.clear();
    float x0 = position.x;
    float x1 = x0 + size.x;
    float y0 = position.y;
    float y1 = y0 + size.y;
    drawInfo.vertices.push_back({{x0, y0}, color});
    drawInfo.vertices.push_back({{x1, y0}, color});
    drawInfo.vertices.push_back({{x1, y1}, color});
    drawInfo.vertices.push_back({{x0, y1}, color});

    drawInfo.indices.clear();
    drawInfo.indices.push_back(baseIndex + 0);
    drawInfo.indices.push_back(baseIndex + 1);
    drawInfo.indices.push_back(baseIndex + 2);
    drawInfo.indices.push_back(baseIndex + 0);
    drawInfo.indices.push_back(baseIndex + 2);
    drawInfo.indices.push_back(baseIndex + 3);
  };
  void addChild(std::shared_ptr<Element> element) override {
    auto child = std::dynamic_pointer_cast<Rect>(element);
    child->position = position + child->position * size;
    child->size = child->size * size;
    children.push_back(child);
  }
};
template <auto CallbackListMemberPtr> class EventListener {
public:
  EventListener(
      EngineContext &context, std::size_t inputCallbackSystemIndex,
      std::decay_t<
          decltype(context.inputCallbackSystems[inputCallbackSystemIndex].*
                   CallbackListMemberPtr)>::value_type callback)
      : context{context}, inputCallbackSystemIndex{inputCallbackSystemIndex} {
    auto &callbackList =
        context.inputCallbackSystems[inputCallbackSystemIndex].*
        CallbackListMemberPtr;
    callbackList.push_back(callback);
    callbackIndex = callbackList.size() - 1;
  }
  ~EventListener() {
    auto &callbackList =
        context.inputCallbackSystems[inputCallbackSystemIndex].*
        CallbackListMemberPtr;
    callbackList.erase(callbackList.begin() + callbackIndex);
  }

private:
  std::size_t inputCallbackSystemIndex;
  std::size_t callbackIndex;

protected:
  EngineContext &context;
};
class Button
    : public Rect,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton> {
public:
  Button(EngineContext &context, std::size_t inputCallbackSystemIndex,
         glm::vec2 position, glm::vec2 size, glm::vec3 color,
         std::function<void(int, int, int)> onClick)
      : Rect(position, size, color), onClick(onClick),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            context, inputCallbackSystemIndex,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }) {}
  void mouseButtonCallback(int button, int action, int mods) {
    glm::vec2 normalizedCursorPos =
        static_cast<glm::vec2>(context.input.cursorPos) /
            static_cast<glm::vec2>(context.window.size) * glm::vec2(2.0) -
        glm::vec2(1.0);
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(normalizedCursorPos, min)) &&
        glm::all(glm::lessThanEqual(normalizedCursorPos, max)))
      onClick(button, action, mods);
  }
  void addChild(std::shared_ptr<Element> element) override {
    Rect::addChild(element);
    auto child = std::dynamic_pointer_cast<Button>(children.back());
  }
  ~Button() {}

private:
  std::function<void(int, int, int)> onClick;
};
class Text : public Element {
public:
  Text(const std::string &content) : content{content} {}
  std::string content;
  void updateDrawInfo(uint32_t baseIndex) override {}
};
// time to cook b*tch
template <typename T>
concept DerivedFromElement = std::is_base_of_v<Element, std::decay_t<T>>;
template <typename T> struct isDerivedFromEventListener {
private:
  template <auto Ptr>
  static constexpr std::true_type test(const EventListener<Ptr> *);
  static constexpr std::false_type test(...);

public:
  static constexpr bool value = decltype(test(std::declval<T *>()))::value;
};

template <typename T>
concept DerivedFromEventListener =
    isDerivedFromEventListener<std::decay_t<T>>::value;
class View {
public:
  View(EngineContext &context) : context{context} {
    context.inputCallbackSystems.emplace_back();
    inputCallbackSystemIndex = context.inputCallbackSystems.size() - 1;
  }

  template <DerivedFromElement T, typename... Args>
    requires(!DerivedFromEventListener<T>)
  void addElement(Args &&...args) {
    elements.emplace_back(std::make_shared<T>(std::forward<Args>(args)...));
  }

  template <DerivedFromElement T, typename... Args>
    requires DerivedFromEventListener<T>
  void addElement(Args &&...args) {
    elements.emplace_back(std::make_shared<T>(context, inputCallbackSystemIndex,
                                              std::forward<Args>(args)...));
  }
  auto begin() { return elements.begin(); }
  auto end() { return elements.end(); }
  auto begin() const { return elements.begin(); }
  auto end() const { return elements.end(); }

  void setCurrent() {
    context.currentInputCallbackSystemIndex = inputCallbackSystemIndex;
  }

  ~View() {
    context.inputCallbackSystems.erase(context.inputCallbackSystems.begin() +
                                       inputCallbackSystemIndex);
  }

private:
  EngineContext &context;

  std::vector<std::shared_ptr<Element>> elements;
  std::size_t inputCallbackSystemIndex;
};

} // namespace hudSys
} // namespace vkh
