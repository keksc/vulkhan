#include "../src/vkh/frustum.hpp"

#include <cassert>
#include <cmath>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <iostream>
#include <random>

bool inside(const std::array<glm::vec4, 6> &planes, glm::vec3 point) {
  for (auto plane : planes)
    if (glm::dot(plane, glm::vec4(point, 1)) < 0)
      return false;
  return true;
}

int main() {
  const auto identity = vkh::extractFrustumPlanes(glm::mat4(1));
  assert(inside(identity, {0, 0, .5f}));
  assert(!inside(identity, {0, 0, -.5f}));
  assert(!inside(identity, {0, 0, 1.5f}));
  assert(!inside(identity, {1.1f, 0, .5f}));
  const auto camera =
      glm::lookAtRH(glm::vec3(3, 2, 4), glm::vec3(0), glm::vec3(0, 1, 0));
  std::mt19937 random(14);
  std::uniform_real_distribution<float> coord(-20.f, 20.f);
  for (bool reversed : {false, true}) {
    auto proj = glm::perspectiveRH_ZO(
        glm::radians(75.f), 1.7f, reversed ? 15.f : .2f, reversed ? .2f : 15.f);
    proj[1][1] *= -1;
    const auto matrix = proj * camera;
    const auto planes = vkh::extractFrustumPlanes(matrix);
    for (int i = 0; i < 10000; ++i) {
      const glm::vec3 point(coord(random), coord(random), coord(random));
      const auto clip = matrix * glm::vec4(point, 1);
      const bool expected = clip.x >= -clip.w && clip.x <= clip.w &&
                            clip.y >= -clip.w && clip.y <= clip.w &&
                            clip.z >= 0 && clip.z <= clip.w;
      assert(inside(planes, point) == expected);
    }
  }
  glm::mat4 infinite(0);
  infinite[0][0] = 1;
  infinite[1][1] = -1;
  infinite[2][3] = -1;
  infinite[3][2] = .2f;
  const auto planes = vkh::extractFrustumPlanes(infinite);
  for (auto p : planes)
    for (int c = 0; c < 4; ++c)
      assert(std::isfinite(p[c]));
  assert(inside(planes, {0, 0, -1000000.f}));
  assert(!inside(planes, {0, 0, -.1f}));
  std::cout << "Vulkan clip tests: identity, perspective, reversed Z and "
               "infinite far passed\n";
}
