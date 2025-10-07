#pragma once

#include "button.hpp"
#include "element.hpp"
#include "filePicker.hpp"
#include "rect.hpp"

#include <GLFW/glfw3.h>
#include <magic_enum/magic_enum.hpp>

namespace vkh {
namespace hud {
class Canvas : public Rect {
public:
  Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         decltype(Rect::imageIndex) bgImageIndex);
  void saveToFile(const std::filesystem::path &path);
  void reset();
  std::shared_ptr<FilePicker> filePicker;
  void loadFromFile(const std::filesystem::path &path);

private:
  bool handleKey(int key, int scancode, int action, int mods) override;
  bool handleMouseButton(int button, int action, int mods) override;
  bool handleCursorPosition(double xpos, double ypos) override;
  bool handleDrop(int count, const char **paths) override;

  void initBaseElements();
  void removeFileBtns();

  std::shared_ptr<Element> elementBeingAdded;
  std::shared_ptr<hud::Text> modeText;
  std::shared_ptr<hud::Rect> modeBg;

  std::vector<std::shared_ptr<Button>> fileBtns;

  enum Mode { Select, Text, Rect, Line } mode{Mode::Select};
};
} // namespace hud
} // namespace vkh
