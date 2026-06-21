#pragma once

#include "clickable.hpp"

namespace UI {
class Text;
class RectImg;
class Button : public Clickable {
public:
  Button(vkh::hud::View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         size_t imageIndex,
         std::function<void(int button, int action, int mods)> onClick,
         const std::string &label);

  std::shared_ptr<RectImg> bg;
  std::shared_ptr<Text> label;
};
} // namespace UI
