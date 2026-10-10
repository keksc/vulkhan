#pragma once
#include <glm/glm.hpp>

namespace vkh::input {
class MouseLook {
public:
  void reset() { initialized = false; }
  glm::dvec2 sample(glm::dvec2 position, bool captured) {
    if (!captured) {
      reset();
      return {};
    }
    const auto delta = initialized ? position - previous : glm::dvec2{};
    previous = position;
    initialized = true;
    return delta;
  }

private:
  glm::dvec2 previous{};
  bool initialized = false;
};
} // namespace vkh::input
