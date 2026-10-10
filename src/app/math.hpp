#pragma once
#include <array>
#include <glm/glm.hpp>
#include <vector>
namespace app {
std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &matrix);
bool sphereInFrustum(const std::array<glm::vec4, 6> &planes,
                     const glm::vec3 &center, float radius);
std::vector<glm::mat4> genTransform();
} // namespace app
