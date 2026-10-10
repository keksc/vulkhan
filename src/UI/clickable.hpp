#pragma once

#include "../vkh/systems/hud/element.hpp"

namespace UI {
class Clickable : public vkh::hud::Element {
public:
  Clickable(vkh::hud::View &view, Element *parent, glm::vec2 position,
            glm::vec2 size,
            std::function<void(int button, int action, int mods)> onClick);
  void setCallback(std::function<void(int, int, int)> newCallback);

  bool isPressed() const { return pressed; }

private:
  bool pressed = false;
  std::function<void(int button, int action, int mods)> onClick;
  bool handleMouseButton(int button, int action, int mods) override;
};
} // namespace UI
