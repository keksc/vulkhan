#pragma once

#include "text.hpp"

namespace UI {
class Line;
template <typename T> class AutoUpdateString;
class TextInput : public Text {
public:
  TextInput(vkh::hud::View &view, Element *parent, glm::vec2 position,
            const std::string &content = "", bool selected = false);

  ~TextInput() override;
  bool selected{false};
  bool isPositionInside(const glm::vec2 &position) override;
  void addToDrawInfo(vkh::hud::DrawInfo &info, float depth) override;
  void refreshLayout() override {
    Text::refreshLayout();
    setCursor(currentCursorPos);
  }

private:
  bool handleCharacter(unsigned int codepoint) override;
  bool handleKey(int key, int scancode, int action, int mods) override;
  bool handleMouseButton(int button, int action, int mods) override;
  bool handleCursorPosition(double xpos, double ypos) override;

  size_t currentCursorPos = 0;
  void setCursor(size_t newPos);

  std::shared_ptr<Line> cursor;
};
} // namespace UI
