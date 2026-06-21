#pragma once

#include <glm/glm.hpp>

#include "../vkh/systems/hud/element.hpp"

namespace UI {
class Scrollable : public vkh::hud::Element {
public:
  Scrollable(vkh::hud::View &view, Element *parent, glm::vec2 position,
             glm::vec2 size, glm::vec2 scale, glm::vec2 min, glm::vec2 max);

  bool handleScroll(double xoffset, double yoffset) override;

  glm::vec2 scale{};
  glm::vec2 min{};
  glm::vec2 max{};

private:
  glm::vec2 scroll{};
};
} // namespace UI
