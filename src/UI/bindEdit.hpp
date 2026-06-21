#pragma once

#include "button.hpp"

namespace UI {
class BindEdit : public Button {
public:
  BindEdit(vkh::hud::View &view, Element *parent, glm::vec2 position,
              glm::vec2 size, size_t imageIndex,
              std::function<void(int, int, int)> onClick,
              const std::string &label)
      : Button{view, parent, position, size, imageIndex, onClick, label} {}
  vkh::input::Action action;
};
} // namespace UI
