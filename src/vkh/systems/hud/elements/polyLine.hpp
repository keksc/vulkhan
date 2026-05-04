#pragma once

#include "element.hpp"
#include <vector>

namespace vkh {
namespace hud {
class PolyLine : public Element {
public:
  PolyLine(View &view, Element *parent, glm::vec2 position, glm::vec3 color);
  void addPoint(glm::vec2 point); // point is relative to canvas
  glm::vec3 color{};
  std::vector<glm::vec2> points; // relative to canvas

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;
  bool isPositionInside(const glm::vec2 &pos) override;
};
} // namespace hud
} // namespace vkh
