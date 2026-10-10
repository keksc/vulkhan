#pragma once
#include "../vkh/systems/hud/element.hpp"
namespace UI {
class RectImg;
class Slider : public vkh::hud::Element {
public:
  Slider(vkh::hud::View &view, Element *parent, glm::vec2 position,
         glm::vec2 size, glm::vec2 bounds, float value = {});
  glm::vec2 bounds;
  float value{};
  float step = 1.f;
  std::function<void(float)> onChange;
  std::function<void(float)> onCommit;
  void addToDrawInfo(vkh::hud::DrawInfo &info, float depth) override;

private:
  enum class Orientation { Horizontal, Vertical };
  Orientation orientation;
  std::shared_ptr<RectImg> box, fill;
  bool selected{};
  bool handleMouseButton(int button, int action, int mods) override;
  bool handleCursorPosition(double xpos, double ypos) override;
  void updateFromCursor();
  void updateBoxPosition(float p);
};
} // namespace UI
