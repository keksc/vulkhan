#include "../src/vkh/mouseLook.hpp"
#include <cassert>
#include <iostream>

int main() {
  vkh::input::MouseLook look;
  assert(look.sample({400, 300}, true) == glm::dvec2{});
  assert(look.sample({405, 298}, true) == glm::dvec2(5, -2));
  // Menu motion must not become camera motion.
  assert(look.sample({1500, 900}, false) == glm::dvec2{});
  assert(look.sample({10, 20}, false) == glm::dvec2{});
  assert(look.sample({7000, -2000}, true) == glm::dvec2{});
  assert(look.sample({7002, -2003}, true) == glm::dvec2(2, -3));
  // Pause/resume in the same event poll, with no inactive frame sampled.
  look.reset();
  assert(look.sample({300, 250}, true) == glm::dvec2{});
  assert(look.sample({299, 251}, true) == glm::dvec2(-1, 1));
  // Focus loss/recovery and repeated cursor recaptures.
  for (int i = 0; i < 100; ++i) {
    look.reset();
    assert(look.sample({i * 1000, -i * 500}, true) == glm::dvec2{});
  }
  std::cout << "Mouse capture transition checks passed\n";
}
