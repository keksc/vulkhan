#pragma once

#include "element.hpp"
#include "eventListener.hpp"
#include "rect.hpp"

namespace vkh {
namespace hud {
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
        static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x0, y0}, color);
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x1, y0}, color);
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x1, y1}, color);
    drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{x0, y1}, color);

    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 1);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 0);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 2);
    drawInfo.solidColorTriangleIndices.emplace_back(baseIndex + 3);

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
} // namespace hud
} // namespace vkh
