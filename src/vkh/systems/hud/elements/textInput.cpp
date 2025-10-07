#include "textInput.hpp"

namespace vkh::hud {

TextInput::TextInput(View &view, Element *parent, glm::vec2 position,
                     const std::string &content, const glm::vec3 &bgColor)
    : Rect(view, parent, position, glm::vec2{}, 1) {
  text = addChild<Text>(position, content);
  size = text->size;
}
bool TextInput::handleCharacter(unsigned int codepoint) {
  // if (selectedTextInput != this)
  //   return;
  if (!selected)
    return false;
  text->content += static_cast<char>(codepoint);
  size = text->size;
  return true;
}
bool TextInput::handleKey(int key, int scancode, int action, int mods) {
  if (!selected)
    return false;
  if (key == GLFW_KEY_V && mods == GLFW_MOD_CONTROL &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (text->content.empty())
      return true;
    const char *clipboard = glfwGetClipboardString(NULL);
    if (!clipboard)
      return true;
    text->content += clipboard;
    size = text->size;
    return true;
  }
  if (key == GLFW_KEY_C && mods == GLFW_MOD_CONTROL &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (text->content.empty())
      return true;
    glfwSetClipboardString(NULL, text->content.c_str());
    size = text->size;
    return true;
  }
  if (key == GLFW_KEY_BACKSPACE &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (text->content.empty())
      return true;
    text->content.pop_back();
    size = text->size;
    return true;
  }
  if (key == GLFW_KEY_ENTER &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    text->content.push_back('\n');
    size = text->size;
    return true;
  }
  if (key == GLFW_KEY_ESCAPE) {
    selected = false;
    return true;
  }
  return false;
}
bool TextInput::handleMouseButton(int button, int action, int mods) {
  if(button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
    return false;
  selected = isCursorInside();
  return selected;
}
bool TextInput::handleCursorPosition(double xpos, double ypos) {
  glfwSetCursor(view.context.window, isCursorInside()
                                         ? view.context.window.cursors.ibeam
                                         : view.context.window.cursors.arrow);
  return false;
}
} // namespace vkh::hud
