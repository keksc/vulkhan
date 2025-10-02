#pragma once

#include "element.hpp"
#include "rect.hpp"
#include "text.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <print>

namespace vkh {
namespace hud {
class FilePicker : public Element {
public:
  enum Mode { Save, Open };

  FilePicker(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
             Mode mode)
      : mode{mode}, Element(view, parent, position, size) {
    path = addChild<Text>(glm::vec2{});
    list = addChild<Text>(glm::vec2{0.f, TextSys::glyphRange.maxSizeY});
    addChild<Rect>(glm::vec2{}, glm::vec2{1.f}, 0);
    flush();
  }

  Mode mode;
  std::shared_ptr<Text> path;
  std::shared_ptr<Text> list;

private:
  void flush() {
    list->content.clear();
    for (const auto &entry : std::filesystem::directory_iterator(".")) {
      auto pathStr = std::filesystem::relative(entry.path()).string();
      if (pathStr.starts_with(path->content))
        list->content += pathStr + '\n';
    }
  }
  bool handleCharacter(unsigned int codepoint) override {
    path->content += static_cast<char>(codepoint);
    flush();
    return true;
  }
  bool handleKey(int key, int scancode, int action, int mods) override {
    if (!(key == GLFW_KEY_BACKSPACE &&
          (action == GLFW_PRESS || action == GLFW_REPEAT)))
      return true;
    if (path->content.empty())
      return true;
    path->content.pop_back();
    flush();
    return true;
  }
};
} // namespace hud
} // namespace vkh
