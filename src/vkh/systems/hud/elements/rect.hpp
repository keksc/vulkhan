#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Rect : public Element {
public:
  Rect(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       unsigned short imageIndex);

  unsigned short imageIndex;

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;
};
} // namespace hud
} // namespace vkh
