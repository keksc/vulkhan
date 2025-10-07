#pragma once

#include "element.hpp"
#include "text.hpp"

#include <GLFW/glfw3.h>
#include <print>

namespace vkh {
namespace hud {
class FilePicker : public Element {
public:
  enum Mode { Save, Open };

  FilePicker(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
             Mode mode);

  Mode mode;
  std::shared_ptr<Text> path;
  std::shared_ptr<Text> list;

private:
  void flush();
  bool handleCharacter(unsigned int codepoint) override;
  bool handleKey(int key, int scancode, int action, int mods) override;
};
} // namespace hud
} // namespace vkh
