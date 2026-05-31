#pragma once

#include "element.hpp"

namespace vkh {
namespace hud {
class Polygon : public Element {
public:
  struct Vertex {
    glm::vec2 pos{};
    glm::vec2 uv{};
    Vertex operator+(Vertex other) {
      return Vertex{.pos = pos + other.pos, .uv = uv};
    };
  };

  Polygon(View &view, Element *parent, const std::vector<Vertex> &newVertices,
          unsigned short imageIndex);

  void update();
  void setVertices(const std::vector<Vertex> &vertices);

  unsigned short imageIndex;

protected:
  void addToDrawInfo(DrawInfo &drawInfo, float depth) override;
  std::vector<Vertex> vertices;

  glm::vec2 g;
};
} // namespace hud
} // namespace vkh
