#include "../src/UI/polygonGeometry.hpp"
#include "../src/graphicsPresets.hpp"
#include <cassert>
#include <iostream>

int main() {
  using namespace UI::geometry;
  std::vector<glm::vec2> shape{{0, 0},     {1, 0},     {1, 1},   {.6f, 1},
                               {.6f, .4f}, {.4f, .4f}, {.4f, 1}, {0, 1}};
  for (bool reverse : {false, true}) {
    if (reverse)
      std::reverse(shape.begin(), shape.end());
    auto triangles = triangulate(shape);
    assert(triangles.size() == 18);
    float area = 0;
    for (size_t i = 0; i < triangles.size(); i += 3) {
      auto a = shape[triangles[i]], b = shape[triangles[i + 1]],
           c = shape[triangles[i + 2]];
      area += std::abs(cross(b - a, c - a)) * .5f;
      assert(contains(shape, (a + b + c) / 3.f));
    }
    assert(std::abs(area - .88f) < 1e-5f);
    assert(!contains(shape, {.5f, .8f}));
    assert(contains(shape, {.2f, .8f}));
    assert(contains(shape, {.6f, .7f})); // edge
    auto transformed = shape;
    for (auto &p : transformed)
      p = glm::vec2(-.75f, .3f) + p * glm::vec2(1.8f, .27f);
    for (int x = 0; x < 99; ++x)
      for (int y = 0; y < 99; ++y) {
        glm::vec2 p((x + .3f) / 100, (y + .3f) / 100);
        assert(contains(shape, p) ==
               contains(transformed,
                        glm::vec2(-.75f, .3f) + p * glm::vec2(1.8f, .27f)));
      }
  }
  assert(triangulate(std::vector<glm::vec2>{{0, 0}, {1, 0}, {2, 0}}).empty());
  assert(triangulate(std::vector<glm::vec2>{{0, 0}, {1, 1}, {0, 1}, {1, 0}})
             .empty());
  assert(vkh::supportedMsaa(8, 1 | 2 | 4) == 4);
  assert(vkh::supportedMsaa(4, 1 | 4 | 8) == 4);
  assert(vkh::supportedMsaa(2, 1 | 4 | 8) == 1);
  assert(vkh::supportedMsaa(1, 1 | 2 | 4 | 8) == 1);
  assert(!vkh::validMsaa(0) && !vkh::validMsaa(3) && vkh::validMsaa(8));
  std::cout << "Concave polygon triangulation, winding, boundary hits, "
               "non-square transforms and MSAA capability selection passed\n";
}
