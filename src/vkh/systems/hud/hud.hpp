#pragma once

#include "../system.hpp"

#include "drawInfo.hpp"
#include "solidColor.hpp"
#include "text.hpp"

#include <memory>

namespace vkh {
class Image2D;
namespace hud {
class View;
class Element;
} // namespace hud
class HudSys : public System {
public:
  HudSys(EngineContext &context);
  void setView(hud::View *newView);
  hud::View *getView();
  void render();

  TextSys textSys;
  SolidColorSys solidColorSys;

private:
  void addToDraw(hud::Element &element, float &depth,
                 float oneOverElementCount);
  void update();

  hud::DrawInfo drawInfo;
  hud::View *view{};
}; // namespace hudSys
} // namespace vkh
