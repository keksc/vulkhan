#include "textInput.hpp"

#include "../vkh/systems/hud/view.hpp"

#include "line.hpp"
#include "text.hpp"

namespace UI {
TextInput::TextInput(vkh::hud::View &view, Element *parent, glm::vec2 position,
                     const std::string &content, bool selected)
    : Text(view, parent, position, content), selected{selected} {
  cursor = addChild<Line>(glm::vec2{0.f}, glm::vec2{0.f, 1.f}, glm::vec3{1.f});
}
void TextInput::setCursor(size_t newPos) {
  if (newPos > content.size())
    newPos = content.size();

  glm::vec2 localOffset{0.f, 0.f};
  float maxSizeY = vkh::TextSys::glyphRange.maxSizeY;

  for (size_t i = 0; i < newPos; i++) {
    char c = content[i];

    if (c == '\n') {
      localOffset.x = 0.f;
      localOffset.y += maxSizeY * getSizeMultiplicator();
      continue;
    }

    if (!vkh::TextSys::glyphRange.glyphs.count(c))
      continue;

    auto &ch = vkh::TextSys::glyphRange.glyphs[c];

    localOffset.x += ch.advance * getSizeMultiplicator();
  }

  cursor->setAbsolutePosition(this->getAbsolutePosition() + localOffset);
  cursor->setSize(glm::vec2{0.f, 1.f / newlineLocations.size()});
  currentCursorPos = newPos;
}
bool TextInput::handleCharacter(unsigned int codepoint) {
  if (!selected)
    return false;

  content.insert(currentCursorPos, 1, static_cast<char>(codepoint));

  setCursor(currentCursorPos + 1);
  return true;
}
bool TextInput::handleKey(int key, int scancode, int action, int mods) {
  if (!selected)
    return false;

  // COPY/PASTE
  if (key == GLFW_KEY_V && mods == GLFW_MOD_CONTROL &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    const char *clipboard = glfwGetClipboardString(view.context.window);
    if (!clipboard)
      return true;
    content.insert(currentCursorPos, clipboard);
    setCursor(currentCursorPos + strlen(clipboard));
    return true;
  }
  if (key == GLFW_KEY_C && mods == GLFW_MOD_CONTROL &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (content.empty())
      return true;
    glfwSetClipboardString(NULL, content.c_str());
    return true;
  }

  // BACKSPACE
  if (key == GLFW_KEY_BACKSPACE &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (content.empty() || currentCursorPos == 0)
      return true;

    if (mods & GLFW_MOD_CONTROL) {
      size_t lastSpace = content.substr(0, currentCursorPos).find_last_of(' ');
      size_t lastNewline =
          content.substr(0, currentCursorPos).find_last_of('\n');
      size_t lastValid = lastSpace > lastNewline ? lastSpace : lastNewline;
      if (lastValid == std::string::npos) {
        content.erase(0, currentCursorPos); // Erase up to current pos
        setCursor(0);
      } else {
        size_t countToErase = currentCursorPos - lastValid;
        content.erase(lastValid, countToErase);
        setCursor(lastValid);
      }
      if (lastValid == lastNewline && lastValid != std::string::npos)
        newlineLocations.erase(std::find(newlineLocations.begin(),
                                         newlineLocations.end(), lastNewline));
      return true;
    }

    if (content[currentCursorPos] == '\n')
      newlineLocations.erase(std::find(
          newlineLocations.begin(), newlineLocations.end(), currentCursorPos));
    content.erase(currentCursorPos - 1, 1);
    setCursor(currentCursorPos - 1);
    return true;
  }

  // NEWLINE
  if (key == GLFW_KEY_ENTER &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    content.insert(currentCursorPos, "\n");
    newlineLocations.emplace_back(currentCursorPos);
    setCursor(currentCursorPos + 1);
    return true;
  }

  // ESC
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    selected = false;
    return true;
  }

  // DIRECTIONAL KEYS
  if (key == GLFW_KEY_UP && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    setCursor(0);
    return true;
  }
  if (key == GLFW_KEY_RIGHT &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    setCursor(currentCursorPos + 1);
    return true;
  }
  if (key == GLFW_KEY_DOWN && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    setCursor(content.size() - 1);
    return true;
  }
  if (key == GLFW_KEY_LEFT && (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (currentCursorPos > 0) // size_t only supports positive numbers
    {
      setCursor(currentCursorPos - 1);
      return true;
    }
  }

  return false;
}
bool TextInput::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
    return false;

  selected = isCursorInside();
  if (!selected)
    return false;

  glm::vec2 &mousePos = view.context.input.cursorPos;
  glm::vec2 relative = mousePos - this->getAbsolutePosition();

  float paragraph = relative.y / vkh::TextSys::glyphRange.maxSizeY;
  unsigned int paragraphNumber = static_cast<int>(paragraph);

  return true;
}
bool TextInput::handleCursorPosition(double xpos, double ypos) {
  glfwSetCursor(view.context.window, isCursorInside()
                                         ? view.context.window.cursors.ibeam
                                         : view.context.window.cursors.arrow);
  return false;
}
} // namespace UI
