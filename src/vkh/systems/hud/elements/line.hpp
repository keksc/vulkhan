#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Line : public Element {
public:
  Line(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       glm::vec3 color);
  glm::vec3 color{};

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;
};
} // namespace hud
} // namespace vkh
