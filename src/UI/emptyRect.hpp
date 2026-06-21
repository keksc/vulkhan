#pragma once

#include "../vkh/systems/hud/element.hpp"

namespace UI {
class EmptyRect : public vkh::hud::Element {
public:
  EmptyRect(vkh::hud::View &view, Element *parent, glm::vec2 position,
            glm::vec2 size);
  void addToDrawInfo(vkh::hud::DrawInfo &drawInfo, float depth) override;
};
} // namespace UI
