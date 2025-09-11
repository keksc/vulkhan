#pragma once

#include "eventListener.hpp"
#include "rect.hpp"
#include "text.hpp"

namespace vkh {
namespace hud {
class TextInput
    : public Rect,
      public EventListener<&EngineContext::InputCallbackSystem::character>,
      public EventListener<&EngineContext::InputCallbackSystem::mouseButton>,
      public EventListener<&EngineContext::InputCallbackSystem::key>,
      public EventListener<
          &EngineContext::InputCallbackSystem::cursorPosition> {
public:
  TextInput(View &view, Element *parent, glm::vec2 position,
            const std::string &content = "", const glm::vec3 &bgColor = {})
      : Rect(view, parent, position, glm::vec2{}, 1),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view, std::bind(&TextInput::characterCallback, this,
                            std::placeholders::_1)),
        EventListener<&EngineContext::InputCallbackSystem::mouseButton>(
            view, std::bind(&TextInput::mouseButtonCallback, this,
                            std::placeholders::_1, std::placeholders::_2,
                            std::placeholders::_3)),
        EventListener<&EngineContext::InputCallbackSystem::key>(
            view, std::bind(&TextInput::keyCallback, this,
                            std::placeholders::_1, std::placeholders::_3,
                            std::placeholders::_3, std::placeholders::_4)),
        EventListener<&EngineContext::InputCallbackSystem::cursorPosition>(
            view, std::bind(&TextInput::cursorPositionCallback, this,
                            std::placeholders::_1, std::placeholders::_2)) {
    text = addChild<Text>(position, content);
    size = text->size;
  }

  auto &getContent() const { return text->content; }

private:
  void characterCallback(unsigned int codepoint) {
    // if (selectedTextInput != this)
    //   return;
    if (!selected)
      return;
    text->content += static_cast<char>(codepoint);
    size = text->size;
  }
  void keyCallback(int key, int scancode, int action, int mods) {
    if (key == GLFW_KEY_V && mods == GLFW_MOD_CONTROL &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
      if (!selected)
        return;
      if (text->content.empty())
        return;
      const char *clipboard = glfwGetClipboardString(NULL);
      if (!clipboard)
        return;
      text->content += clipboard;
      size = text->size;
      return;
    }
    if (key == GLFW_KEY_C && mods == GLFW_MOD_CONTROL &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
      if (!selected)
        return;
      if (text->content.empty())
        return;
      glfwSetClipboardString(NULL, text->content.c_str());
      size = text->size;
      return;
    }
    if (key == GLFW_KEY_BACKSPACE &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
      if (!selected)
        return;
      if (text->content.empty())
        return;
      text->content.pop_back();
      size = text->size;
      return;
    }
    if (key == GLFW_KEY_ENTER &&
        (action == GLFW_PRESS || action == GLFW_REPEAT)) {
      if (!selected)
        return;
      text->content.push_back('\n');
      size = text->size;
      return;
    }
    if (key == GLFW_KEY_ESCAPE)
      selected = false;
  }
  void mouseButtonCallback(int button, int action, int mods) {
    selected = isCursorInside();
  }

  void cursorPositionCallback(double xpos, double ypos) {
    glfwSetCursor(view.context.window, isCursorInside()
                                           ? view.context.window.cursors.ibeam
                                           : view.context.window.cursors.arrow);
  }

  bool selected{false};
  std::shared_ptr<Text> text;
};
} // namespace hud
} // namespace vkh
