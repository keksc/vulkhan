#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Clickable : public Element {
public:
  Clickable(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         std::function<void(int button, int action, int mods)> onClick);
  void setCallback(std::function<void(int, int, int)> newCallback);

private:
  std::function<void(int button, int action, int mods)> onClick;
  bool handleMouseButton(int button, int action, int mods) override;
};
} // namespace hud
} // namespace vkh
