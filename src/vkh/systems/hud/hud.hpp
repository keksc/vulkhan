#pragma once

#include "../system.hpp"
#include "elements/drawInfo.hpp"
#include "elements/element.hpp"
#include "elements/view.hpp"
#include "solidColor.hpp"
#include "text.hpp"

namespace vkh {
class HudSys : public System {
public:
  HudSys(EngineContext &context);
  void setView(hud::View *newView);
  hud::View *getView();
  void render();

private:
  void createPipeline();
  void addToDraw(std::vector<std::shared_ptr<hud::Element>> &elements,
                 float &depth);
  void update();

  std::unique_ptr<GraphicsPipeline> pipeline;
  TextSys textSys;
  SolidColorSys solidColorSys;

  std::shared_ptr<Image> fontAtlas;
  hud::DrawInfo drawInfo;
  hud::View *view{};
}; // namespace hudSys
} // namespace vkh
