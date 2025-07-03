#pragma once

#include "eventListener.hpp"
#include "rect.hpp"
#include "text.hpp"

namespace vkh {
namespace hud {
class TextInput
    : public Text,
      public EventListener<&EngineContext::InputCallbackSystem::character>,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<&EngineContext::InputCallbackSystem::key> {
public:
  TextInput(View &view, Element *parent, glm::vec2 position,
            const std::string &content = "", const glm::vec3 &bgColor = {})
      : Text(view, parent, position, content),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view,
            [this](int button, int action, int mods) {
              mouseButtonCallback(button, action, mods);
            }),
        EventListener<&EngineContext::InputCallbackSystem::key>(
            view, [this](int key, int scancode, int action, int mods) {
              keyCallback(key, scancode, action, mods);
            }) {
    bg = addChild<Rect>(position, size, bgColor);
  }

private:
  void characterCallback(unsigned int codepoint) {
    // if (selectedTextInput != this)
    //   return;
    if (!selected)
      return;
    content += static_cast<char>(codepoint);
    flushSize();
    bg->size = size;
  }
  void keyCallback(int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_BACKSPACE &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
      if (!selected)
        return;
      if (content.empty())
        return;
      content.pop_back();
      flushSize();
      bg->size = size;
      return;
    }
    if (key == GLFW_KEY_ENTER &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
      if (!selected)
        return;
      content.push_back('\n');
      return;
    }
    if (key == GLFW_KEY_ESCAPE)
      selected = false;
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

  bool selected{false};
  std::shared_ptr<Rect> bg;
};
} // namespace hud
} // namespace vkh
