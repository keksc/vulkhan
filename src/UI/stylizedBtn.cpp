#include "stylizedBtn.hpp"

#include "../vkh/systems/hud/element.hpp"
#include "polygon.hpp"
#include "text.hpp"

#include <cmath>
#include <memory>

namespace UI {
void animateBubbly(vkh::EngineContext &context, vkh::hud::Element &element) {
  for (const auto &child : element.children) {
    if (auto btn = std::dynamic_pointer_cast<StylizedBtn>(child)) {
      btn->animate(context.time);
    }
    animateBubbly(context, *child);
  }
}

StylizedBtn::StylizedBtn(
    vkh::hud::View &view, Element *parent, glm::vec2 position, glm::vec2 size,
    size_t imageIndex,
    std::function<void(int button, int action, int mods)> onClick,
    const std::string &label)
    : Clickable(view, parent, position, size, onClick) {
  outer = addChild<Polygon>(outerVertices, 0);
  inner = addChild<Polygon>(innerVertices, 0);
  this->label = addChild<Text>(glm::vec2{}, label);
}

void StylizedBtn::animate(float time) {
  const float wobbleSpeed = 12.0f;
  const float wobbleAmount = 0.015f; // Reduced to prevent clipping

  // Static buffers to avoid re-allocation, but we still need to fetch current
  // vertices or use the base ones to avoid "drift" if we were doing additive
  // updates. Since we use sin/cos on base positions, we copy base once.
  static thread_local std::vector<Polygon::Vertex> ov = outerVertices;
  static thread_local std::vector<Polygon::Vertex> iv = innerVertices;

  for (int i = 0; i < 4; ++i) {
    float offset = i * 1.57f;

    // Outer wobble
    float ox = sin(time * wobbleSpeed + offset) * wobbleAmount;
    float oy = cos(time * (wobbleSpeed * 1.1f) + offset * 1.3f) * wobbleAmount;
    ov[i].pos = outerVertices[i].pos + glm::vec2(ox, oy);

    // Inner wobble - independent of outer for P3 "variable thickness" look
    // Using different frequencies and phases so they move relative to each
    // other
    float ix = sin(time * (wobbleSpeed * 0.8f) + offset * 2.1f) *
               (wobbleAmount * 1.2f);
    float iy = cos(time * (wobbleSpeed * 1.3f) + offset * 0.5f) *
               (wobbleAmount * 1.2f);
    iv[i].pos = innerVertices[i].pos + glm::vec2(ix, iy);
  }

  outer->setVertices(ov);
  inner->setVertices(iv);

  if (isCursorInside()) {
    label->setPosition(
        glm::vec2(sin(time * 20.f) * 0.005f, cos(time * 25.f) * 0.005f));
  } else {
    label->setPosition(glm::vec2(0.f));
  }
}
} // namespace UI
