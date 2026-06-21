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

private:
  const std::vector<Polygon::Vertex> outerVertices = {
      Polygon::Vertex{
          .pos = glm::vec2{0.05f, 0.08f},
      },
      Polygon::Vertex{
          .pos = glm::vec2{0.95f, 0.05f},
      },
      Polygon::Vertex{
          .pos = glm::vec2{0.97f, 0.92f},
      },
      Polygon::Vertex{
          .pos = glm::vec2{0.08f, 0.97f},
      },
  };
  const std::vector<Polygon::Vertex> innerVertices = {
      Polygon::Vertex{
          .pos = glm::vec2{.10f, .12f},
          .uv = glm::vec2{.1f, .1f},
      },
      Polygon::Vertex{
          .pos = glm::vec2{.90f, .10f},
          .uv = glm::vec2{.1f, .1f},
      },
      Polygon::Vertex{
          .pos = glm::vec2{.92f, .88f},
          .uv = glm::vec2{.1f, .1f},
      },
      Polygon::Vertex{
          .pos = glm::vec2{.13f, .92f},
          .uv = glm::vec2{.1f, .1f},
      },
  };
};

void animateBubbly(vkh::EngineContext &context, vkh::hud::Element &element);
} // namespace UI
