#pragma once

#include "eventListener.hpp"
#include "rect.hpp"
#include "text.hpp"

namespace vkh {
namespace hud {
class Button
    : public Rect,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton> {
public:
  Button(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         decltype(Rect::imageIndex) imageIndex, std::function<void(int, int, int)> onClick,
         const std::string &label)
      : Rect(view, parent, position, size, imageIndex), onClick{onClick},
        EventListener(
            view, std::bind(&Button::mouseButtonCallback, this, std::placeholders::_1,
                            std::placeholders::_2, std::placeholders::_3)),
        label{addChild<Text>(glm::vec2{}, label)} {}

  void setCallback(std::function<void(int, int, int)> newCallback) {
    onClick = std::move(newCallback);
  }

  std::shared_ptr<Text> label;

private:
  std::function<void(int, int, int)> onClick;
  void mouseButtonCallback(int button, int action, int mods) {
    if (isCursorInside())
      onClick(button, action, mods);
  }
};
} // namespace hud
} // namespace vkh
