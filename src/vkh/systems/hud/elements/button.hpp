#pragma once

#include "rectImg.hpp"

namespace vkh {
namespace hud {
class Text;
class Button : public Rect {
public:
  Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         decltype(Rect::imageIndex) imageIndex,
         std::function<void(int button, int action, int mods)> onClick,
         const std::string &label);
  void setCallback(std::function<void(int, int, int)> newCallback);

  std::shared_ptr<Text> label;

private:
  std::function<void(int button, int action, int mods)> onClick;
  bool handleMouseButton(int button, int action, int mods) override;
};
} // namespace hud
} // namespace vkh
