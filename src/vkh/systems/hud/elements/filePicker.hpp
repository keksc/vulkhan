#pragma once

#include "element.hpp"
#include "eventListener.hpp"
#include "rect.hpp"
#include "text.hpp"

#include <GLFW/glfw3.h>
#include <filesystem>
#include <print>

namespace vkh {
namespace hud {
class FilePicker
    : public Element,
      public EventListener<&EngineContext::InputCallbackSystem::character>,
      public EventListener<&EngineContext::InputCallbackSystem::key> {
public:
  enum Mode { Save, Open };

  FilePicker(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
             Mode mode)
      : mode{mode}, Element(view, parent, position, size),
        EventListener<&EngineContext::InputCallbackSystem::character>(
            view,
            [this](unsigned int codepoint) { characterCallback(codepoint); }),
        EventListener<&EngineContext::InputCallbackSystem::key>(
            view, [this](int key, int scancode, int action, int mods) {
              keyCallback(key, scancode, action, mods);
            }) {
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
  void characterCallback(unsigned int codepoint) {
    path->content += static_cast<char>(codepoint);
    flush();
  }
  void keyCallback(int key, int scancode, int action, int mods) {
    if (!(key == GLFW_KEY_BACKSPACE &&
          (action == GLFW_PRESS || action == GLFW_REPEAT)))
      return;
    if (path->content.empty())
      return;
    path->content.pop_back();
    flush();
  }
};
} // namespace hud
} // namespace vkh
