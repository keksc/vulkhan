#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class RectImg;
class Slider : public Element {
public:
  Slider(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec2 bounds, float value = {});

  glm::vec2 bounds;
  float value{};

private:
  enum class Orientation { Horizontal, Vertical };
  Orientation orientation;

  std::shared_ptr<RectImg> box;
  bool selected{};

  bool handleMouseButton(int button, int action, int mods) override;
  bool handleCursorPosition(double xpos, double ypos) override;
  void updateBoxPosition(float p);
};

} // namespace hud
} // namespace vkh
