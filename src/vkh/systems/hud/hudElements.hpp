#pragma once

#include <GLFW/glfw3.h>
#include <fmt/format.h>
#include <glm/glm.hpp>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "text.hpp"

namespace vkh {
namespace hud {
class Element;
template <auto CallbackListPtr> class EventListener;
class View {
public:
  View(EngineContext &context) : context{context} {
    context.inputCallbackSystems[this] = {};
  }

  template <typename T, typename... Args>
  std::shared_ptr<T> addElement(Args &&...args) {
    auto element = std::make_shared<T>(*this, std::forward<Args>(args)...);
    elements.push_back(element);
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
struct SolidColorVertex {
  glm::vec2 position{};
  glm::vec3 color{};
};
struct DrawInfo {
  std::vector<SolidColorVertex> solidColorVertices;
  std::vector<uint32_t> solidColorIndices;
  std::vector<TextSys::Vertex> textVertices;
  std::vector<uint32_t> textIndices;
};
class Element {
public:
  Element(View &view, glm::vec2 position, glm::vec2 size)
      : position{position}, size{size}, view{view} {}
  ~Element() { children.clear(); }

  template <typename T, typename... Args>
  std::shared_ptr<T> addChild(Args &&...args) {
    auto element = std::make_shared<T>(view, std::forward<Args>(args)...);
    element->fitIntoParent(position, size);
    children.push_back(element);
    return element;
  }

  virtual void addToDrawInfo(DrawInfo &drawInfo) = 0;
  std::vector<std::shared_ptr<Element>> children;

  glm::vec2 position{};
  glm::vec2 size{};

  virtual void fitIntoParent(glm::vec2 &parentPosition,
                             glm::vec2 &parentSize) = 0;

protected:
  View &view;
};
class Rect : public Element {
public:
  Rect(View &view, glm::vec2 position, glm::vec2 size, glm::vec3 color)
      : Element(view, position, size), color{color} {};
  glm::vec3 color{};

  void fitIntoParent(glm::vec2 &parentPosition,
                     glm::vec2 &parentSize) override {
    position = parentPosition + position * parentSize;
    size = parentSize * size;
  };

protected:
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
};
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
class Button
    : public Rect,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton> {
public:
  Button(View &view, glm::vec2 position, glm::vec2 size, glm::vec3 color,
         std::function<void(int, int, int)> onClick)
      : Rect(view, position, size, color), onClick{onClick},
        EventListener(view, [this](int button, int action, int mods) {
          mouseButtonCallback(button, action, mods);
        }) {}

private:
  std::function<void(int, int, int)> onClick;
  void mouseButtonCallback(int button, int action, int mods) {
    glm::vec2 normalizedCursorPos =
        static_cast<glm::vec2>(view.context.input.cursorPos) /
            static_cast<glm::vec2>(view.context.window.size) * glm::vec2(2.0) -
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
  Text(View &view, glm::vec2 position) : Element(view, position, {0.f, 0.f}) {}
  Text(View &view, glm::vec2 position, const std::string &content)
      : Element(view, position, {0.f, 0.f}), content{content} {}
  std::string content;

  void fitIntoParent(glm::vec2 &parentPosition,
                     glm::vec2 &parentSize) override {
    position = parentPosition + position * parentSize;
    // the text size is divided by the aspect ratio in the vertex shader, so
    // here we should take this into account and add returns to break the text
  };

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    glm::vec2 cursor = position;
    float maxX = cursor.x;

    float maxSizeY = TextSys::glyphRange.maxSizeY;
    for (auto &c : content) {
      if (c == '\n') {
        cursor.x = position.x;
        cursor.y += maxSizeY;
        continue;
      }
      if (!TextSys::glyphRange.glyphs.count(c))
        continue;
      uint32_t baseIndex = drawInfo.textVertices.size();
      auto &ch = TextSys::glyphRange.glyphs[c];

      // Calculate vertex positions for this character
      float x0 = cursor.x + ch.offset.x;
      float x1 = x0 + ch.size.x;
      float y0 = maxSizeY + cursor.y + ch.offset.y;
      float y1 = y0 + ch.size.y;

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
      cursor.x += ch.advance;
      maxX = glm::max(cursor.x, maxX);
    }
    size = glm::vec2{maxX, cursor.y + maxSizeY} - position;
  }
};
class TextInput
    : public Text,
      public EventListener<&EngineContext::InputCallbackSystem::character>,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton> {
public:
  TextInput(View &view, glm::vec2 position)
      : Text(view, position),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view, [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }) {}
  TextInput(View &view, glm::vec2 position, const std::string &content)
      : Text(view, position, content),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view, [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }) {}

private:
  void characterCallback(unsigned int codepoint) {
    // if (selectedTextInput != this)
    //   return;
    if (!selected)
      return;
    char c = static_cast<char>(codepoint);
    content.push_back(c);
  }
  void mouseButtonCallback(int button, int action, int mods) {
    glm::vec2 normalizedCursorPos =
        static_cast<glm::vec2>(view.context.input.cursorPos) /
            static_cast<glm::vec2>(view.context.window.size) * glm::vec2(2.0) -
        glm::vec2(1.0);
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(normalizedCursorPos, min)) &&
        glm::all(glm::lessThanEqual(normalizedCursorPos, max)))
      selected = true;
    else
      selected = false;
  }

  bool selected = false;
};
class Slider
    : public Element,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<
          &EngineContext::InputCallbackSystem::cursorPosition> {
public:
  Slider(View &view, glm::vec2 position, glm::vec2 size, glm::vec2 bounds,
         glm::vec3 color)
      : Element(view, position, size),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            view, [this](double xpos,
                         double ypos) { cursorPositionCallback(xpos, ypos); }),
        color{color}, bounds{bounds} {}
  glm::vec3 color;
  glm::vec2 bounds;
  float value{};

  void fitIntoParent(glm::vec2 &parentPosition,
                     glm::vec2 &parentSize) override {
    position = parentPosition + position * parentSize;
    size = parentSize * size;
  };

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    uint32_t baseIndex = drawInfo.solidColorVertices.size();

    float p = value / (bounds.y - bounds.x);
    glm::vec2 center = glm::mix(position, position + size, p);

    float x0 = center.x - size.x * .1f;
    float x1 = x0 + size.x * .2f;
    float y0 = center.y - size.y * .1f;
    float y1 = y0 + size.y * .2f;

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

    boxPosition = {x0, y0};
    boxSize = glm::vec2{x1, y1} - boxPosition;
  };

private:
  void mouseButtonCallback(int button, int action, int mods) {
    glm::vec2 normalizedCursorPos =
        static_cast<glm::vec2>(view.context.input.cursorPos) /
            static_cast<glm::vec2>(view.context.window.size) * glm::vec2(2.0) -
        glm::vec2(1.0);
    const glm::vec2 min = glm::min(boxPosition, boxPosition + boxSize);
    const glm::vec2 max = glm::max(boxPosition, boxPosition + boxSize);

    selected = false;

    if (!(glm::all(glm::greaterThanEqual(normalizedCursorPos, min)) &&
          glm::all(glm::lessThanEqual(normalizedCursorPos, max))))
      return;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    if (action != GLFW_PRESS)
      return;
    selected = true;
  }
  void cursorPositionCallback(double xpos, double ypos) {
    static int n = 0;
    if (!selected)
      return;
    fmt::println("{}", n);
    n++;
  }
  glm::vec2 boxPosition{};
  glm::vec2 boxSize{};
  bool selected{};
};
} // namespace hud
} // namespace vkh
