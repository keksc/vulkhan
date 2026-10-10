#include "textInput.hpp"

#include "../vkh/systems/hud/view.hpp"

#include "canvas.hpp"
#include "line.hpp"
#include "text.hpp"
#include "textEditing.hpp"

namespace UI {
TextInput::TextInput(vkh::hud::View &view, Element *parent, glm::vec2 position,
                     const std::string &content, bool selected)
    : Text(view, parent, position, content), selected{selected} {
  cursor = std::make_shared<Line>(view, nullptr, glm::vec2{}, glm::vec2{},
                                  glm::vec3{1.f});
  setCursor(content.size());
  if (selected)
    view.focusedTextInput = this;
}
TextInput::~TextInput() {
  if (view.focusedTextInput == this)
    view.focusedTextInput = nullptr;
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
      localOffset.y += maxSizeY * renderedScale();
      continue;
    }

    if (!vkh::TextSys::glyphRange.glyphs.count(c))
      continue;

    auto &ch = vkh::TextSys::glyphRange.glyphs[c];

    localOffset.x += ch.advance * renderedScale();
  }

  cursor->setAbsolutePosition(this->getAbsolutePosition() + localOffset);
  cursor->setAbsoluteSize(glm::vec2{0.f, maxSizeY * renderedScale()});
  currentCursorPos = newPos;
}
bool TextInput::handleCharacter(unsigned int codepoint) {
  if (!selected || view.focusedTextInput != this)
    return false;

  if (!vkh::TextSys::glyphRange.glyphs.contains(static_cast<char>(codepoint)) ||
      codepoint > 126)
    return true;
  content.insert(currentCursorPos, 1, static_cast<char>(codepoint));

  setCursor(currentCursorPos + 1);
  return true;
}
bool TextInput::handleKey(int key, int scancode, int action, int mods) {
  if (!selected || view.focusedTextInput != this)
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
    glfwSetClipboardString(view.context.window, content.c_str());
    return true;
  }

  // BACKSPACE
  if (key == GLFW_KEY_BACKSPACE &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    if (content.empty() || currentCursorPos == 0)
      return true;

    if (mods & GLFW_MOD_CONTROL) {
      const auto start = editing::previousWord(content.str(), currentCursorPos);
      content.erase(start, currentCursorPos - start);
      setCursor(start);
      return true;
    }

    content.erase(currentCursorPos - 1, 1);
    setCursor(currentCursorPos - 1);
    return true;
  }

  // NEWLINE
  if (key == GLFW_KEY_ENTER &&
      (action == GLFW_PRESS || action == GLFW_REPEAT)) {
    content.insert(currentCursorPos, "\n");
    setCursor(currentCursorPos + 1);
    return true;
  }

  // ESC
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    selected = false;
    return true;
  }

  if (action != GLFW_PRESS && action != GLFW_REPEAT)
    return false;
  if (key == GLFW_KEY_DELETE) {
    if (currentCursorPos < content.size())
      content.erase(currentCursorPos, 1);
    setCursor(currentCursorPos);
    return true;
  }
  if (key == GLFW_KEY_HOME || key == GLFW_KEY_END) {
    setCursor(key == GLFW_KEY_HOME
                  ? editing::lineStart(content.str(), currentCursorPos)
                  : editing::lineEnd(content.str(), currentCursorPos));
    return true;
  }
  if (key == GLFW_KEY_LEFT || key == GLFW_KEY_RIGHT) {
    setCursor(key == GLFW_KEY_LEFT
                  ? (currentCursorPos ? currentCursorPos - 1 : 0)
                  : currentCursorPos + 1);
    return true;
  }
  if (key == GLFW_KEY_UP || key == GLFW_KEY_DOWN) {
    const auto start = editing::lineStart(content.str(), currentCursorPos);
    const auto column = currentCursorPos - start;
    size_t target = start;
    if (key == GLFW_KEY_UP)
      target = start ? editing::lineStart(content.str(), start - 1) : 0;
    else {
      const auto end = editing::lineEnd(content.str(), start);
      target = end == content.size() ? end : end + 1;
    }
    setCursor(
        std::min(target + column, editing::lineEnd(content.str(), target)));
    return true;
  }

  return false;
}
bool TextInput::handleMouseButton(int button, int action, int mods) {
  if (button != GLFW_MOUSE_BUTTON_LEFT || action != GLFW_PRESS)
    return false;

  selected = isCursorInside();
  if (selected)
    view.focusedTextInput = this;
  else if (view.focusedTextInput == this)
    view.focusedTextInput = nullptr;
  if (!selected || view.focusedTextInput != this)
    return false;

  const glm::vec2 relative =
      view.context.input.cursorPos - getAbsolutePosition();
  setCursor(editing::hit(
      content.str(), relative.x, relative.y,
      vkh::TextSys::glyphRange.maxSizeY * renderedScale(), [this](char c) {
        const auto it = vkh::TextSys::glyphRange.glyphs.find(c);
        return it == vkh::TextSys::glyphRange.glyphs.end()
                   ? 0.f
                   : it->second.advance * renderedScale();
      }));
  if (dynamic_cast<Canvas *>(parent))
    return false;

  return true;
}
bool TextInput::handleCursorPosition(double xpos, double ypos) {
  glfwSetCursor(view.context.window, isCursorInside()
                                         ? view.context.window.cursors.ibeam
                                         : view.context.window.cursors.arrow);
  return false;
}
bool TextInput::isPositionInside(const glm::vec2 &position) {
  const auto size = glm::max(
      getAbsoluteSize(),
      glm::vec2{vkh::TextSys::glyphRange.maxSizeY * renderedScale() * .5f,
                vkh::TextSys::glyphRange.maxSizeY * renderedScale()});
  const auto local = position - getAbsolutePosition();
  return local.x >= 0 && local.y >= 0 && local.x <= size.x && local.y <= size.y;
}
void TextInput::addToDrawInfo(vkh::hud::DrawInfo &info, float depth) {
  Text::addToDrawInfo(info, depth);
  setCursor(currentCursorPos);
  if (selected && view.focusedTextInput == this)
    static_cast<vkh::hud::Element &>(*cursor).addToDrawInfo(info, depth);
}
} // namespace UI
