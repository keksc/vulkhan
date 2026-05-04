#pragma once

#include "rectImg.hpp"

namespace vkh {
namespace hud {
class Text;
class EmptyRect;
class Button;
class FilePicker;
class Canvas : public RectImg {
public:
  Canvas(View &view, Element *parent, glm::vec2 position, glm::vec2 size,
         decltype(RectImg::imageIndex) bgImageIndex);
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

  std::shared_ptr<Element> selectedElement;
  std::shared_ptr<hud::Text> modeText;
  std::shared_ptr<hud::RectImg> modeBg;
  std::shared_ptr<hud::EmptyRect> selectIndicator;

  std::vector<std::shared_ptr<Button>> fileBtns;

  enum class Mode { Select, Text, Rect, Line, Freehand } mode{Mode::Select};
  enum class Interaction { None, Creating, Moving, Resizing } interaction{Interaction::None};
  glm::vec2 dragOffset;
  glm::vec3 currentColor{1.f, 1.f, 1.f};
};
} // namespace hud
} // namespace vkh
