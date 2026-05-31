#pragma once

#include "../system.hpp"

#include "elements/drawInfo.hpp"
#include "solidColor.hpp"
#include "text.hpp"

#include <memory>

namespace vkh {
class GraphicsPipeline;
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
  void createPipeline();
  void addToDraw(hud::Element &element, float &depth,
                 float oneOverElementCount);
  void update();

  std::unique_ptr<GraphicsPipeline> pipeline;

  std::shared_ptr<Image> fontAtlas;
  hud::DrawInfo drawInfo;
  hud::View *view{};
}; // namespace hudSys
} // namespace vkh
