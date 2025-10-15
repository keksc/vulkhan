#pragma once

#include "rectImg.hpp"
#include "text.hpp"

namespace vkh {
namespace hud {
class Button : public Rect {
public:
  Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         decltype(Rect::imageIndex) imageIndex,
         std::function<void(int, int, int)> onClick, const std::string &label);
  void setCallback(std::function<void(int, int, int)> newCallback);

  std::shared_ptr<Text> label;

private:
  std::function<void(int, int, int)> onClick;
  bool handleMouseButton(int button, int action, int mods) override;
};
} // namespace hud
} // namespace vkh
