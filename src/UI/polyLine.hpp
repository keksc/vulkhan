#pragma once

#include "../vkh/systems/hud/element.hpp"

#include <vector>

namespace UI {
class PolyLine : public vkh::hud::Element {
public:
  PolyLine(vkh::hud::View &view, Element *parent, glm::vec2 position, glm::vec3 color);
  void addPoint(glm::vec2 point); // point is relative to canvas
  glm::vec3 color{};
  std::vector<glm::vec2> points; // relative to canvas

protected:
  void addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) override;
  bool isPositionInside(const glm::vec2 &pos) override;
};
} // namespace UI
