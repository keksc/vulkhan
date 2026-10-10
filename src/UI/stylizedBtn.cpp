#include "stylizedBtn.hpp"
#include "button.hpp"
#include "text.hpp"
#include "theme.hpp"

namespace UI {
void animateBubbly(vkh::EngineContext &context, vkh::hud::Element &element) {
  for (const auto &child : element.children) {
    if (auto button = std::dynamic_pointer_cast<StylizedBtn>(child))
      button->animate(context.time);
    else if (auto button = std::dynamic_pointer_cast<Button>(child))
      button->animate(context.time);
    animateBubbly(context, *child);
  }
}
StylizedBtn::StylizedBtn(vkh::hud::View &view, Element *parent,
                         glm::vec2 position, glm::vec2 size, size_t imageIndex,
                         std::function<void(int, int, int)> onClick,
                         const std::string &text)
    : Clickable(view, parent, position, size, std::move(onClick)) {
  customImage = imageIndex;
  theme::initialize(view);
  outer = addChild<Polygon>(outerVertices, theme::color(view, theme::border));
  inner = addChild<Polygon>(outerVertices,
                            imageIndex ? imageIndex
                                       : theme::color(view, theme::button));
  inner->setPosition({.012f, .035f});
  inner->setSize({.976f, .93f});
  label = addChild<Text>(glm::vec2{}, text, glm::vec3{1.f});
  animate(0);
}
bool StylizedBtn::isPositionInside(const glm::vec2 &position) {
  return outer && outer->isPositionInside(position);
}
void StylizedBtn::animate(float) {
  const bool hovered = isCursorInside();
  outer->imageIndex =
      theme::color(view, hovered ? theme::accent : theme::border);
  inner->imageIndex = customImage
                          ? customImage
                          : theme::color(view, isPressed() ? theme::pressed
                                               : hovered   ? theme::hover
                                                           : theme::button);
  label->setSizeMultiplicator(.85f);
  const auto textSize = label->getAbsoluteSize(),
             available = glm::abs(getAbsoluteSize()) * glm::vec2(.78f, .58f);
  float fit =
      std::min(1.f, std::min(available.x / std::max(textSize.x, .001f),
                             available.y / std::max(textSize.y, .001f)));
  label->setSizeMultiplicator(.85f * fit);
  label->setAbsolutePosition(getAbsolutePosition() +
                             (getAbsoluteSize() - label->getAbsoluteSize()) *
                                 .5f);
}
} // namespace UI
