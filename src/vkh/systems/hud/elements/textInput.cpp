#include "textInput.hpp"

#include "text.hpp"
#include "view.hpp"

namespace vkh::hud {

TextInput::TextInput(View &view, Element *parent, glm::vec2 position,
                     const std::string &content, bool selected)
    : Text(view, parent, position, content), selected{selected} {
}
bool TextInput::handleCharacter(unsigned int codepoint) {
  // if (selectedTextInput != this)
  //   return;
  if (!selected)
    return false;
  content += static_cast<char>(codepoint);
  absSize = absSize;
  return true;
}
bool TextInput::handleKey(int key, int scancode, int action, int mods) {
  if (!selected)
    return false;
  if (key == GLFW_KEY_V && mods == GLFW_MOD_CONTROL &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    const char *clipboard = glfwGetClipboardString(NULL);
    if (!clipboard)
      return true;
    content += clipboard;
    absSize = absSize;
    return true;
  }
  if (key == GLFW_KEY_C && mods == GLFW_MOD_CONTROL &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (content.empty())
      return true;
    glfwSetClipboardString(NULL, content.c_str());
    absSize = absSize;
    return true;
  }
  if (key == GLFW_KEY_BACKSPACE &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (content.empty())
      return true;

    if (mods & GLFW_MOD_CONTROL) {
      size_t lastSpace = content.find_last_of(' ');
      if (lastSpace == std::string::npos) {
        content.clear();
      } else {
        content.erase(lastSpace);
      }
      absSize = absSize;
      return true;
    }
    content.pop_back();
    absSize = absSize;
    return true;
  }
  if (key == GLFW_KEY_ENTER &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    content.push_back('\n');
    absSize = absSize;
    return true;
  }
  if (key == GLFW_KEY_ESCAPE) {
    selected = false;
    return true;
  }
  return false;
}
bool TextInput::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
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
