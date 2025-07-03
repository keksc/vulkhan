#pragma once

#include <glm/glm.hpp>

#include "element.hpp"
#include "eventListener.hpp"
#include "rect.hpp"
#include "slider.hpp"

namespace vkh {
namespace hud {

class ColorPicker
    : public Element,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<
          &EngineContext::InputCallbackSystem::cursorPosition> {
public:
  ColorPicker(View &view, Element *parent, glm::vec2 position, glm::vec2 size)
      : Element(view, parent, position, size),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            view, [this](double xpos, double ypos) {
              cursorPositionCallback(xpos, ypos);
            }) {
    saturationSlider = addChild<Slider>(
        glm::vec2{1.f, 0.f}, glm::vec2{.2f, 1.f}, glm::vec3{1.f},
        glm::vec3{.3f}, glm::vec2{0.f, 1.f}, 1.f);
    colorViewer =
        addChild<Rect>(glm::vec2{1.f, 0.f}, glm::vec2{1.f}, glm::vec3{0.f});
  }

  glm::vec3 getSelectedColor() const { return selectedColor; }

protected:
  void addToDrawInfo(DrawInfo &drawInfo) override {
    const float x0 = position.x;
    const float y0 = position.y;
    const float width = size.x;
    const float height = size.y;

    // Render gradient from left (red) to right (blue), top (green)
    const int stepsX = 20;
    const int stepsY = 20;
    const float dx = width / stepsX;
    const float dy = height / stepsY;

    uint32_t baseIndex =
        static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());

    for (int y = 0; y < stepsY; ++y) {
      for (int x = 0; x < stepsX; ++x) {
        float px = x0 + x * dx;
        float py = y0 + y * dy;

        glm::vec3 colorA = getColorAtUV(float(x) / stepsX, float(y) / stepsY);
        glm::vec3 colorB =
            getColorAtUV(float(x + 1) / stepsX, float(y) / stepsY);
        glm::vec3 colorC =
            getColorAtUV(float(x + 1) / stepsX, float(y + 1) / stepsY);
        glm::vec3 colorD =
            getColorAtUV(float(x) / stepsX, float(y + 1) / stepsY);

        uint32_t i =
            static_cast<uint32_t>(drawInfo.solidColorTriangleVertices.size());
        drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{px, py},
                                                         colorA);
        drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{px + dx, py},
                                                         colorB);
        drawInfo.solidColorTriangleVertices.emplace_back(
            glm::vec2{px + dx, py + dy}, colorC);
        drawInfo.solidColorTriangleVertices.emplace_back(glm::vec2{px, py + dy},
                                                         colorD);

        // Two triangles per quad
        drawInfo.solidColorTriangleIndices.emplace_back(i);
        drawInfo.solidColorTriangleIndices.emplace_back(i + 1);
        drawInfo.solidColorTriangleIndices.emplace_back(i + 2);

        drawInfo.solidColorTriangleIndices.emplace_back(i);
        drawInfo.solidColorTriangleIndices.emplace_back(i + 2);
        drawInfo.solidColorTriangleIndices.emplace_back(i + 3);
      }
    }
  }

  glm::vec3 selectedColor{1.0f, 1.0f, 1.0f};

private:
  bool selected = false;

  void mouseButtonCallback(int button, int action, int) {

    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);

    selected = false;
    if (button != GLFW_MOUSE_BUTTON_LEFT)
      return;
    if (action != GLFW_PRESS)
      return;
    if (!(glm::all(glm::greaterThanEqual(cursorPos, min)) &&
          glm::all(glm::lessThanEqual(cursorPos, max))))
      return;
    selected = true;

    updateColor(cursorPos);
  }

  void cursorPositionCallback(double xpos, double ypos) {
    if (!selected)
      return;

    const auto &cursorPos = view.context.input.cursorPos;
    updateColor(cursorPos);
  }

  void updateColor(glm::vec2 cursorPos) {
    glm::vec2 uv = (cursorPos - position) / size;
    uv = glm::clamp(uv, glm::vec2(0.0f), glm::vec2(1.0f));
    selectedColor = getColorAtUV(uv.x, uv.y);
  }

  glm::vec3 hsvToRgb(float h, float s, float v) const {
    float c = v * s;
    float x = c * (1 - std::abs(fmod(h * 6.0f, 2.0f) - 1));
    float m = v - c;

    glm::vec3 rgb;

    if (h < 1.0f / 6.0f)
      rgb = {c, x, 0};
    else if (h < 2.0f / 6.0f)
      rgb = {x, c, 0};
    else if (h < 3.0f / 6.0f)
      rgb = {0, c, x};
    else if (h < 4.0f / 6.0f)
      rgb = {0, x, c};
    else if (h < 5.0f / 6.0f)
      rgb = {x, 0, c};
    else
      rgb = {c, 0, x};

    return rgb + glm::vec3(m);
  }

  glm::vec3 getColorAtUV(float u, float v) const {
    float hue = glm::clamp(u, 0.f, 1.f);       // full spectrum left → right
    float sat = saturationSlider->value;       // optional: set to v
    float val = glm::clamp(1.f - v, 0.f, 1.f); // top = bright, bottom = black
    return hsvToRgb(hue, sat, val);
  }
  std::shared_ptr<Rect> colorViewer;
  std::shared_ptr<Slider> saturationSlider;
};
} // namespace hud
} // namespace vkh
