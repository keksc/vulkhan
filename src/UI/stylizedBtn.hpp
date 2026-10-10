#pragma once

#include "../vkh/systems/hud/view.hpp"
#include "clickable.hpp"
#include "polygon.hpp"

namespace vkh {
class EngineContext;
} // namespace vkh

namespace UI {
class Polygon;
class Text;
class StylizedBtn : public Clickable {
public:
  StylizedBtn(vkh::hud::View &view, Element *parent, glm::vec2 position,
              glm::vec2 size, size_t imageIndex,
              std::function<void(int button, int action, int mods)> onClick,
              const std::string &label);

  std::shared_ptr<Polygon> outer;
  std::shared_ptr<Polygon> inner;
  std::shared_ptr<Text> label;

  void animate(float t);
  bool isPositionInside(const glm::vec2 &position) override;

private:
  size_t customImage = 0;
  const std::vector<Polygon::Vertex> outerVertices = {
      {{.04f, .12f}}, {{.90f, .12f}}, {{.96f, .28f}}, {{.96f, .76f}},
      {{.90f, .88f}}, {{.10f, .88f}}, {{.04f, .72f}}};
};

void animateBubbly(vkh::EngineContext &context, vkh::hud::Element &element);
} // namespace UI
