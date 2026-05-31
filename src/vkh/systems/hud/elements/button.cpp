#include "button.hpp"

#include "rectImg.hpp"
#include "text.hpp"

namespace vkh::hud {
Button::Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
               size_t imageIndex, std::function<void(int, int, int)> onClick,
               const std::string &label)
    : Clickable(view, parent, position, size, onClick),
      bg{addChild<RectImg>(glm::vec2{}, glm::vec2{1.f}, imageIndex)},
      label{addChild<Text>(glm::vec2{}, label)} {}
} // namespace vkh::hud
