#pragma once

#include "element.hpp"
#include "rect.hpp"

namespace vkh {
namespace hud {

class Slider : public Element {

public:
  Slider(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         glm::vec2 bounds, float value = {});

  glm::vec2 bounds;
  float value{};

private:
  enum class Orientation { Horizontal, Vertical };
  Orientation orientation;

  std::shared_ptr<Rect> box;
  bool selected{};

  bool handleMouseButton(int button, int action, int mods) override;
  bool handleCursorPosition(double xpos, double ypos) override;
  void updateBoxPosition(float p);
};

} // namespace hud
} // namespace vkh
