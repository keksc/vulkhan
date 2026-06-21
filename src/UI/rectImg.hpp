#pragma once

#include "../vkh/systems/hud/element.hpp"

namespace UI {
class RectImg : public vkh::hud::Element {
public:
  RectImg(vkh::hud::View &view, Element *parent, glm::vec2 position,
          glm::vec2 size, size_t imageIndex);

  size_t imageIndex;

protected:
  void addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) override;
};
} // namespace UI
