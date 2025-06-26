#pragma once

#include <GLFW/glfw3.h>
#include <algorithm>
#include <fmt/core.h>
#include <fmt/format.h>
#include <glm/glm.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/string_cast.hpp>
#include <vulkan/vulkan_core.h>

#include <functional>
#include <memory>
#include <type_traits>
#include <utility>

#include "solidColor.hpp"
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
    auto element =
        std::make_shared<T>(*this, nullptr, std::forward<Args>(args)...);
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
  std::vector<SolidColorSys::Vertex> lineVertices;
};
class Element {
public:
  Element(View &view, Element *parent, glm::vec2 position, glm::vec2 offset)
      : position{position}, size{offset}, view{view} {
    if (parent) {
      this->position = parent->position + position;
      this->size = parent->size * size;
    }
  }
  Element(const Element &) = delete;
  Element &operator=(const Element &) = delete;
  ~Element() { children.clear(); }

  template <typename T, typename... Args>
  std::shared_ptr<T> addChild(Args &&...args) {
    auto element = std::make_shared<T>(view, this, std::forward<Args>(args)...);
    children.push_back(element);
    return element;
  }

  virtual void addToDrawInfo(DrawInfo &drawInfo) {};
  std::vector<std::shared_ptr<Element>> children;

  glm::vec2 position{};
  glm::vec2 size{};

protected:
  View &view;
};
class Rect : public Element {
public:
  Rect(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       glm::vec3 color)
      : Element(view, parent, position, size), color{color} {};
  glm::vec3 color{};

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    float x0 = position.x;
    float x1 = x0 + size.x;
    float y0 = position.y;
    float y1 = y0 + size.y;

    uint32_t baseIndex =
        static_cast<uint32_t>(drawInfo.solidColorVertices.size());
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
class Line : public Element {
public:
  Line(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       glm::vec3 color)
      : Element(view, parent, position, size), color{color} {};
  glm::vec3 color{};

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    // TODO: this might be optimizable when nothing changes, maybe add a
    // "changed" flag
    drawInfo.lineVertices.push_back({{position.x, position.y}, color});
    drawInfo.lineVertices.push_back(
        {{position.x + size.x, position.y + size.y}, color});
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
  Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec3 color, std::function<void(int, int, int)> onClick)
      : Rect(view, parent, position, size, color), onClick{onClick},
        EventListener(view, [this](int button, int action, int mods) {
          mouseButtonCallback(button, action, mods);
        }) {}

private:
  std::function<void(int, int, int)> onClick;
  void mouseButtonCallback(int button, int action, int mods) {
    const auto &cursorPos = view.context.input.cursorPos;

    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(cursorPos, min)) &&
        glm::all(glm::lessThanEqual(cursorPos, max)))
      onClick(button, action, mods);
  }
};
class Text : public Element {
public:
  Text(View &view, Element *parent, glm::vec2 position,
       const std::string &content = "")
      : Element(view, parent, position, {}), content{content} {}
  std::string content;

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
      uint32_t baseIndex = static_cast<uint32_t>(drawInfo.textVertices.size());
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
  TextInput(View &view, Element *parent, glm::vec2 position)
      : Text(view, parent, position),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view, [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }) {}
  TextInput(View &view, Element *parent, glm::vec2 position,
            const std::string &content)
      : Text(view, parent, position, content),
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
    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(cursorPos, min)) &&
        glm::all(glm::lessThanEqual(cursorPos, max)))
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
  Slider(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec3 color, glm::vec3 bgColor, glm::vec2 bounds, float value = {})
      : Element(view, parent, position, size),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            view, [this](double xpos,
                         double ypos) { cursorPositionCallback(xpos, ypos); }),
        color{color}, bounds{bounds}, value{value} {
    float p = value / (bounds.y - bounds.x);
    centerX = glm::mix(position.x, position.x + size.x, p);
    addChild<Rect>(glm::vec2{}, glm::vec2{1.f}, bgColor);
  }
  glm::vec3 color;
  glm::vec2 bounds;
  float value{};

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {

    float normalizedBoxHalfSize = 10.f / view.context.window.size.x;
    float x0 = centerX - normalizedBoxHalfSize;
    float x1 = x0 + 2.f * normalizedBoxHalfSize;
    float y0 = position.y + .5f * size.y - normalizedBoxHalfSize;
    float y1 = y0 + 2.f * normalizedBoxHalfSize;

    uint32_t baseIndex =
        static_cast<uint32_t>(drawInfo.solidColorVertices.size());
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
    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(boxPosition, boxPosition + boxSize);
    const glm::vec2 max = glm::max(boxPosition, boxPosition + boxSize);

    selected = false;

    if (!(glm::all(glm::greaterThanEqual(cursorPos, min)) &&
          glm::all(glm::lessThanEqual(cursorPos, max))))
      return;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    if (action != GLFW_PRESS)
      return;
    selected = true;
  }
  void cursorPositionCallback(double xpos, double ypos) {
    if (!selected)
      return;

    float normalizedXPos =
        static_cast<float>(xpos) / view.context.window.size.x * 2.f - 1.f;
    float p = (normalizedXPos - position.x) / size.x;
    p = glm::clamp(p, 0.f, 1.f);
    centerX = glm::mix(position.x, position.x + size.x, p);
    value = glm::mix(bounds.x, bounds.y, p);
  }
  glm::vec2 boxPosition{};
  glm::vec2 boxSize{};
  float centerX{};
  bool selected{};
};
class Canvas
    : public Element,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<&EngineContext::InputCallbackSystem::cursorPosition>,
      public EventListener<&EngineContext::InputCallbackSystem::doubleClick> {
public:
  Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec3 bgColor)
      : Element(view, parent, position, size),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            view, [this](double xpos,
                         double ypos) { cursorPositionCallback(xpos, ypos); }),
        EventListener<&EngineContext::InputCallbackSystem::doubleClick>(
            view, [this]() { doubleClickCallback(); }) {
    bg = addChild<Rect>(glm::vec2{}, glm::vec2{1.f}, bgColor);
  }

  glm::vec3 lineColor{};

private:
  void mouseButtonCallback(int button, int action, int mods) {
    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);

    selected = false;

    if (!(glm::all(glm::greaterThanEqual(cursorPos, min)) &&
          glm::all(glm::lessThanEqual(cursorPos, max))))
      return;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    if (action != GLFW_PRESS)
      return;
    selected = true;

    // fix it creating size=0 lines on double clicks
    elementBeingAdded =
        addChild<Line>((cursorPos - position), glm::vec2{}, lineColor);
  }
  void cursorPositionCallback(double xpos, double ypos) {
    if (!selected)
      return;

    const auto &cursorPos = view.context.input.cursorPos;

    elementBeingAdded->size = cursorPos - elementBeingAdded->position;
  }
  void doubleClickCallback() {
    const auto &cursorPos = view.context.input.cursorPos;

    addChild<TextInput>((cursorPos - position));

    std::iter_swap(std::find(children.begin(), children.end(), bg),
                   std::prev(children.end()));
  }
  bool selected{};
  std::shared_ptr<Element> elementBeingAdded;
  std::shared_ptr<Rect> bg;
};
} // namespace hud
} // namespace vkh
