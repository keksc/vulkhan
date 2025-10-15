#pragma once

#include "element.hpp"

namespace vkh::hud {
// Maybe a Rect with a partially transparent image is smarter than this
class EmptyRect : public Element {
public:
  EmptyRect(View &view, Element *parent, glm::vec2 position, glm::vec2 size);
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;
};
} // namespace vkh::hud
