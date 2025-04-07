#pragma once

#include <fmt/core.h>
#include <fmt/format.h>
#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "../../engineContext.hpp"
#include "text.hpp"

namespace vkh {
namespace hudSys {
struct SolidColorVertex {
  glm::vec2 position{};
  glm::vec3 color{};
};
struct DrawInfo {
  std::vector<SolidColorVertex> solidColorVertices;
  std::vector<uint32_t> solidColorIndices;
  std::vector<textSys::Vertex> textVertices;
  std::vector<uint32_t> textIndices;
};
class Element {
public:
  Element(glm::vec2 position, glm::vec2 size)
      : position{position}, size{size} {}
  virtual void addToDrawInfo(DrawInfo &drawInfo) = 0;
  void addChild(std::shared_ptr<Element> element) {
    element->fitIntoParent(position, size);
    children.push_back(element);
  };

protected:
  virtual void fitIntoParent(glm::vec2 &parentPosition,
                             glm::vec2 &parentSize) = 0;

  glm::vec2 position{};
  glm::vec2 size{};

private:
  std::vector<std::shared_ptr<Element>> children;
  friend void addElementToDraw(std::shared_ptr<Element> element);
};
class Rect : public Element {
public:
  Rect(glm::vec2 position, glm::vec2 size, glm::vec3 color)
      : Element(position, size), color{color} {};
  glm::vec3 color{};
  void addToDrawInfo(DrawInfo &drawInfo) override {
    uint32_t baseIndex = drawInfo.solidColorVertices.size();
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    float x0 = position.x;
    float x1 = x0 + size.x;
    float y0 = position.y;
    float y1 = y0 + size.y;
    drawInfo.solidColorVertices.push_back({{x0, y0}, color});
    drawInfo.solidColorVertices.push_back({{x1, y0}, color});
    drawInfo.solidColorVertices.push_back({{x1, y1}, color});
    drawInfo.solidColorVertices.push_back({{x0, y1}, color});

    drawInfo.solidColorIndices.push_back(baseIndex + 0);
    drawInfo.solidColorIndices.push_back(baseIndex + 1);
    drawInfo.solidColorIndices.push_back(baseIndex + 2);
    drawInfo.solidColorIndices.push_back(baseIndex + 0);
    drawInfo.solidColorIndices.push_back(baseIndex + 2);
    drawInfo.solidColorIndices.push_back(baseIndex + 3);
  };
  void fitIntoParent(glm::vec2 &parentPosition,
                     glm::vec2 &parentSize) override {
    position = parentPosition + position * parentSize;
    size = parentSize * size;
  };
};
class EventListenerBase {};
template <auto CallbackListPtr> class EventListener : public EventListenerBase {
public:
  EventListener(
      EngineContext &context, std::size_t inputCallbackSystemIndex,
      std::decay_t<
          decltype(context.inputCallbackSystems[inputCallbackSystemIndex].*
                   CallbackListPtr)>::value_type callback)
      : context{context}, inputCallbackSystemIndex{inputCallbackSystemIndex} {
    auto &callbackList =
        context.inputCallbackSystems[inputCallbackSystemIndex].*CallbackListPtr;
    callbackList.push_back(callback);
    callbackIndex = callbackList.size() - 1;
  }
  ~EventListener() {
    auto &callbackList =
        context.inputCallbackSystems[inputCallbackSystemIndex].*CallbackListPtr;
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
      : Rect(position, size, color), onClick{onClick},
        EventListener(context, inputCallbackSystemIndex,
                      [this](int button, int action, int mods) {
                        mouseButtonCallback(button, action, mods);
                      }) {}

private:
  std::function<void(int, int, int)> onClick;
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
};
class Text : public Element {
public:
  Text(glm::vec2 position) : Element(position, {0.f, 0.f}) {}
  Text(glm::vec2 position, const std::string &content)
      : Element(position, {0.f, 0.f}), content{content} {}
  std::string content;
  void addToDrawInfo(DrawInfo &drawInfo) override {
    float cursorX = position.x;
    float maxSizeY = 0.f;

    for (auto &c : content) {
      uint32_t baseIndex = drawInfo.textVertices.size();
      auto &ch = textSys::glyphs[c];

      // Calculate vertex positions for this character
      float x0 = cursorX + ch.offset.x;
      float x1 = x0 + ch.size.x;
      float y0 = position.y;
      float y1 = ch.size.y + y0;

      maxSizeY = glm::max(maxSizeY, ch.size.y);

      // Add four vertices for this character
      drawInfo.textVertices.push_back(
          {{x0, y0}, {ch.uvOffset.x, ch.uvOffset.y}});
      drawInfo.textVertices.push_back(
          {{x1, y0}, {ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y}});
      drawInfo.textVertices.push_back(
          {{x1, y1},
           {ch.uvOffset.x + ch.uvExtent.x, ch.uvOffset.y + ch.uvExtent.y}});
      drawInfo.textVertices.push_back(
          {{x0, y1}, {ch.uvOffset.x, ch.uvOffset.y + ch.uvExtent.y}});

      // Add indices for this character's quad
      drawInfo.textIndices.push_back(baseIndex + 0);
      drawInfo.textIndices.push_back(baseIndex + 1);
      drawInfo.textIndices.push_back(baseIndex + 2);
      drawInfo.textIndices.push_back(baseIndex + 0);
      drawInfo.textIndices.push_back(baseIndex + 2);
      drawInfo.textIndices.push_back(baseIndex + 3);

      // Move cursor to next character position
      cursorX += ch.advance;
    }
    size.y = maxSizeY;
    size.x = cursorX - position.x;
  }
  void fitIntoParent(glm::vec2 &parentPosition,
                     glm::vec2 &parentSize) override {
    position = parentPosition + position * parentSize;
    // the text size is divided by the aspect ratio in the vertex shader, so
    // here we should take this into account and add returns to break the text
  };
};
class TextInput
    : public Text,
      public EventListener<&EngineContext::InputCallbackSystem::character>,
      public EventListener<
          &EngineContext::InputCallbackSystem::cursorPosition> {
public:
  TextInput(EngineContext &context, std::size_t inputCallbackSystemIndex,
            glm::vec2 position)
      : Text(position),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            context, inputCallbackSystemIndex,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            context, inputCallbackSystemIndex,
            [this](double xpos, double ypos) {
              cursorPositionCallback(xpos, ypos);
            }),
        context{context} {}
  TextInput(EngineContext &context, std::size_t inputCallbackSystemIndex,
            glm::vec2 position, const std::string &content)
      : Text(position, content),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            context, inputCallbackSystemIndex,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            context, inputCallbackSystemIndex,
            [this](double xpos, double ypos) {
              cursorPositionCallback(xpos, ypos);
            }),
        context{context} {}

private:
  void characterCallback(unsigned int codepoint) {
    // if (selectedTextInput != this)
    //   return;
    if (!selected)
      return;
    char c = static_cast<char>(codepoint);
    content.push_back(c);
  }
  void cursorPositionCallback(double xpos, double ypos) {
    glm::vec2 cursorPos = {static_cast<float>(xpos), static_cast<float>(ypos)};
    glm::vec2 normalizedCursorPos =
        cursorPos / static_cast<glm::vec2>(context.window.size) *
            glm::vec2(2.0) -
        glm::vec2(1.0);
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(normalizedCursorPos, min)) &&
        glm::all(glm::lessThanEqual(normalizedCursorPos, max)))
      selected = true;
    else {
      selected = false;
    }
  }
  bool selected = false;
  EngineContext &context;
};
// time to cook b*tch
template <typename T>
concept DerivedFromElement = std::is_base_of_v<Element, std::decay_t<T>>;
template <typename T>
concept DerivedFromEventListener =
    std::is_base_of_v<EventListenerBase, std::decay_t<T>>;
class View {
public:
  View(EngineContext &context) : context{context} {
    context.inputCallbackSystems.emplace_back();
    inputCallbackSystemIndex = context.inputCallbackSystems.size() - 1;
  }

  template <DerivedFromElement T, typename... Args>
  std::shared_ptr<T> createElement(Args &&...args) {
    if constexpr (DerivedFromEventListener<T>) {
      return std::make_shared<T>(context, inputCallbackSystemIndex,
                                 std::forward<Args>(args)...);
    } else {
      return std::make_shared<T>(std::forward<Args>(args)...);
    }
  }

  template <typename T, typename... Args> void addElement(Args &&...args) {
    elements.emplace_back(createElement<T>(std::forward<Args>(args)...));
  }

  // New overload to add existing elements
  void addElement(std::shared_ptr<Element> element) {
    elements.emplace_back(std::move(element));
  }

  auto begin() { return elements.begin(); }
  auto end() { return elements.end(); }
  auto begin() const { return elements.begin(); }
  auto end() const { return elements.end(); }

  ~View() {
    context.inputCallbackSystems.erase(context.inputCallbackSystems.begin() +
                                       inputCallbackSystemIndex);
  }

private:
  EngineContext &context;
  std::vector<std::shared_ptr<Element>> elements;
  std::size_t inputCallbackSystemIndex;
  void setCurrent() {
    context.currentInputCallbackSystemIndex = inputCallbackSystemIndex;
  }

  friend void setView(View &newView);
};
} // namespace hudSys
} // namespace vkh
