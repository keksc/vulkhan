#include "../src/UI/layout.hpp"
#include <cassert>
#include <cmath>
#include <iostream>

int main() {
  for (glm::ivec2 viewport : {glm::ivec2{800, 600},
                              {1920, 1080},
                              {2560, 720},
                              {600, 1200},
                              {400, 300}}) {
    const auto layout = UI::fitCanvas(viewport);
    const glm::vec2 pixels = layout.size * glm::vec2(viewport) / 2.f;
    assert(std::abs(pixels.x / pixels.y - 4.f / 3.f) < .0001f);
    assert(pixels.x <= viewport.x && pixels.y <= viewport.y);
    assert(glm::length(layout.position + layout.size / 2.f) < .0001f);
    // The seven-sided resume button keeps its design proportions.
    const glm::vec2 button = pixels * glm::vec2{.44f, .11f};
    assert(std::abs(button.x / button.y - 16.f / 3.f) < .0001f);
    assert(std::abs(pixels.x - 800.f * layout.scale) < .001f);
  }
  const auto minimized = UI::fitCanvas({800, 0});
  assert(minimized.scale == 0 && minimized.size == glm::vec2{0});
  assert(UI::fitCanvas({1920, 1080}).scale == 1.f);
  assert(UI::fitCanvas({400, 300}).scale == .5f);
  std::cout << "UI resize layout checks passed\n";
}
