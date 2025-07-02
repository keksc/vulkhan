#pragma once

#include "text.hpp"
#include "eventListener.hpp"

namespace vkh {
namespace hud {
class TextInput
    : public Text,
      public EventListener<&EngineContext::InputCallbackSystem::character>,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton> {
public:
  TextInput(View &view, Element *parent, glm::vec2 position)
      : Text(view, parent, position),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view, [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }) {}
  TextInput(View &view, Element *parent, glm::vec2 position,
            const std::string &content)
      : Text(view, parent, position, content),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view, [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }) {}

private:
  void characterCallback(unsigned int codepoint) {
    // if (selectedTextInput != this)
    //   return;
    if (!selected)
      return;
    content += static_cast<char>(codepoint);
  }
  void mouseButtonCallback(int button, int action, int mods) {
    const auto &cursorPos = view.context.input.cursorPos;
    const glm::vec2 min = glm::min(position, position + size);
    const glm::vec2 max = glm::max(position, position + size);
    if (glm::all(glm::greaterThanEqual(cursorPos, min)) &&
        glm::all(glm::lessThanEqual(cursorPos, max)))
      selected = true;
    else
      selected = false;
  }

  bool selected = false;
};
} // namespace hud
} // namespace vkh
