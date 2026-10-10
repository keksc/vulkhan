#pragma once
#include <glm/glm.hpp>

namespace UI {
struct CanvasLayout {
  glm::vec2 position{-1.f};
  glm::vec2 size{};
  float scale = 0.f;
};
// Preserve pixel proportions, centre larger windows, shrink small windows.
inline CanvasLayout fitCanvas(glm::ivec2 viewport) {
  if (viewport.x <= 0 || viewport.y <= 0)
    return {};
  const glm::vec2 pixels{viewport};
  const float scale =
      glm::min(1.f, glm::min(pixels.x / 800.f, pixels.y / 600.f));
  const glm::vec2 size = glm::vec2{800.f, 600.f} * scale / pixels * 2.f;
  return {-size / 2.f, size, scale};
}
} // namespace UI
