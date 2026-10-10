#include "button.hpp"
#include "rectImg.hpp"
#include "text.hpp"
#include "theme.hpp"
#include <algorithm>

namespace UI {
Button::Button(vkh::hud::View &view, Element *parent, glm::vec2 position,
               glm::vec2 size, size_t imageIndex,
               std::function<void(int, int, int)> onClick,
               const std::string &text)
    : Clickable(view, parent, position, size, std::move(onClick)) {
  theme::initialize(view);
  outline = addChild<RectImg>(glm::vec2{}, glm::vec2{1},
                              theme::color(view, theme::border));
  normalImage = imageIndex ? imageIndex : theme::color(view, theme::button);
  hoverImage = imageIndex ? imageIndex : theme::color(view, theme::hover);
  pressedImage = imageIndex ? imageIndex : theme::color(view, theme::pressed);
  bg = addChild<RectImg>(glm::vec2{.008f, .045f}, glm::vec2{.984f, .91f},
                         normalImage);
  label = addChild<Text>(glm::vec2{}, text, glm::vec3{1.f});
  animate(0);
}
void Button::animate(float) {
  const bool hovered = isCursorInside();
  bg->imageIndex = isPressed() ? pressedImage
                   : hovered   ? hoverImage
                               : normalImage;
  outline->imageIndex =
      theme::color(view, hovered ? theme::accent : theme::border);
  label->setSizeMultiplicator(.8f);
  const auto textSize = label->getAbsoluteSize();
  const auto available = glm::abs(getAbsoluteSize()) * glm::vec2(.88f, .70f);
  const float fit =
      std::min(1.f, std::min(available.x / std::max(textSize.x, .001f),
                             available.y / std::max(textSize.y, .001f)));
  label->setSizeMultiplicator(.8f * fit);
  label->setAbsolutePosition(getAbsolutePosition() +
                             (getAbsoluteSize() - label->getAbsoluteSize()) *
                                 .5f);
}
} // namespace UI
