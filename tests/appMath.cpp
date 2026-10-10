#include "../src/app/math.hpp"
#include "../src/rng.hpp"
#include <cassert>
#include <iostream>
int main() {
  auto planes = app::extractFrustumPlanes(glm::mat4{1.f});
  assert(app::sphereInFrustum(planes, {0, 0, .5f}, .1f));
  assert(!app::sphereInFrustum(planes, {2, 0, .5f}, .1f));
  assert(!app::sphereInFrustum(planes, {0, 0, -.2f}, .1f));
  assert(app::sphereInFrustum(planes, {1.05f, 0, .5f}, .1f));
  rng.seed(17);
  for (int i = 0; i < 100; ++i) {
    auto matrices = app::genTransform();
    assert(matrices.size() >= 3 && matrices.size() <= 5);
    for (auto m : matrices) {
      assert(m[3][3] == 1.f);
      for (int axis = 0; axis < 3; ++axis) {
        float scale = glm::length(glm::vec3(m[axis]));
        assert(scale >= .349f && scale <= .551f);
      }
    }
  }
  std::cout << "Extracted Vulkan frustum and bounded attractor checks passed\n";
}
