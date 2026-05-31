#pragma once

#include "vkh/systems/hud/elements/button.hpp"
#include "vkh/systems/hud/elements/clickable.hpp"
#include "vkh/systems/hud/elements/polygon.hpp"
#include "vkh/systems/hud/elements/view.hpp"

namespace vkh {
class EngineContext;
namespace hud {
class Polygon;
class Text;
} // namespace hud
} // namespace vkh

class KeybindEdit : public vkh::hud::Button {
public:
  KeybindEdit(vkh::hud::View &view, Element *parent, glm::vec2 position,
              glm::vec2 size, size_t imageIndex,
              std::function<void(int, int, int)> onClick,
              const std::string &label)
      : Button{view, parent, position, size, imageIndex, onClick, label} {}
  vkh::input::Action action;
};

class UIBtn : public vkh::hud::Clickable {
public:
  UIBtn(vkh::hud::View &view, Element *parent, glm::vec2 position,
        glm::vec2 size, size_t imageIndex,
        std::function<void(int button, int action, int mods)> onClick,
        const std::string &label);

  std::shared_ptr<vkh::hud::Polygon> outer;
  std::shared_ptr<vkh::hud::Polygon> inner;
  std::shared_ptr<vkh::hud::Text> label;

  void animate(float t);

private:
  const std::vector<vkh::hud::Polygon::Vertex> outerVertices = {
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{0.05f, 0.08f},
      },
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{0.95f, 0.05f},
      },
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{0.97f, 0.92f},
      },
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{0.08f, 0.97f},
      },
  };
  const std::vector<vkh::hud::Polygon::Vertex> innerVertices = {
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{.10f, .12f},
          .uv = glm::vec2{.1f, .1f},
      },
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{.90f, .10f},
          .uv = glm::vec2{.1f, .1f},
      },
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{.92f, .88f},
          .uv = glm::vec2{.1f, .1f},
      },
      vkh::hud::Polygon::Vertex{
          .pos = glm::vec2{.13f, .92f},
          .uv = glm::vec2{.1f, .1f},
      },
  };
};

void animateBubbly(vkh::EngineContext &context, vkh::hud::Element &element);
