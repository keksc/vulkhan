#pragma once

#include "../vkh/systems/hud/element.hpp"

namespace UI {
class Line : public vkh::hud::Element {
public:
  Line(vkh::hud::View &view, Element *parent, glm::vec2 position,
       glm::vec2 size, glm::vec3 color = glm::vec3{1.f});
  glm::vec3 color{};

protected:
  void addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) override;
  bool isPositionInside(const glm::vec2 &pos) override;
};
} // namespace UI
