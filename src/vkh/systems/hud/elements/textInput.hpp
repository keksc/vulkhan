#pragma once

#include "rect.hpp"
#include "text.hpp"

namespace vkh {
namespace hud {
class TextInput : public Rect {
public:
  TextInput(View &view, Element *parent, glm::vec2 position,
            const std::string &content = "", const glm::vec3 &bgColor = {});

  auto &getContent() const { return text->content; }

private:
  bool handleCharacter(unsigned int codepoint) override;
  bool handleKey(int key, int scancode, int action, int mods) override;
  bool handleMouseButton(int button, int action, int mods) override;
  bool handleCursorPosition(double xpos, double ypos) override;

  bool selected{false};
  std::shared_ptr<Text> text;
};
} // namespace hud
} // namespace vkh
