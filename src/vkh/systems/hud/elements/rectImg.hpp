#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class RectImg : public Element {
public:
  RectImg(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
       size_t imageIndex);

  size_t imageIndex;

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;
};
} // namespace hud
} // namespace vkh
