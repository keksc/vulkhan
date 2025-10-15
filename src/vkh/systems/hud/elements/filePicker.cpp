#include "filePicker.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>

#include "rectImg.hpp"

namespace vkh::hud {
FilePicker::FilePicker(View &view, Element *parent, glm::vec2 position,
                       glm::vec2 size, Mode mode)
    : mode{mode}, Element(view, parent, position, size) {
  addChild<Rect>(glm::vec2{}, glm::vec2{1.f}, 0);
  list = addChild<Text>(glm::vec2{0.f, TextSys::glyphRange.maxSizeY});
  path = addChild<Text>(glm::vec2{});
  flush();
}
void FilePicker::flush() {
  list->content.clear();
  for (const auto &entry : std::filesystem::directory_iterator(".")) {
    auto pathStr = std::filesystem::relative(entry.path()).string();
    if (pathStr.starts_with(path->content))
      list->content += pathStr + '\n';
  }
}
bool FilePicker::handleCharacter(unsigned int codepoint) {
  path->content += static_cast<char>(codepoint);
  flush();
  return true;
}
bool FilePicker::handleKey(int key, int scancode, int action, int mods) {
  if (action != GLFW_PRESS || action != GLFW_REPEAT)
    return false;
  if (key == GLFW_KEY_ENTER || key == GLFW_KEY_TAB) {
    size_t pos = list->content.find('\n');
    if (pos != std::string::npos) {
      path->content =
          list->content.substr(0, pos); // up to but not including '\n'
      return key == GLFW_KEY_TAB;
    }
    return true;
  }
  if (key != GLFW_KEY_BACKSPACE)
    return false;
  if (path->content.empty())
    return true;
  path->content.pop_back();
  flush();
  return true;
}
} // namespace vkh::hud
