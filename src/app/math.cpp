#include "math.hpp"
#include "../rng.hpp"
#include "../vkh/frustum.hpp"
#include <glm/gtc/matrix_transform.hpp>
#include <random>
std::mt19937 rng{std::random_device{}()};
namespace app {
std::array<glm::vec4, 6> extractFrustumPlanes(const glm::mat4 &m) {
  return vkh::extractFrustumPlanes(m);
}

// Conservative sphere-vs-frustum test: true if any part of the sphere
// could be inside. Used to decide whether a lake's reflection is worth
// rendering this frame at all, before paying for a full extra opaque-
// scene pass for it (see PlanarReflectionSys's per-lake cost note).
bool sphereInFrustum(const std::array<glm::vec4, 6> &planes,
                     const glm::vec3 &center, float radius) {
  for (const auto &p : planes) {
    float distance = glm::dot(glm::vec3(p), center) + p.w;
    if (distance < -radius)
      return false;
  }
  return true;
}

std::vector<glm::mat4> genTransform() {

  std::uniform_int_distribution<uint32_t> countDist(
      3, 5); // 3 to 5 transformations looks great in 3D
  uint32_t numTransforms = countDist(rng);

  // Keep scales strictly bound so the 3D volume remains stable and
  // contained
  std::uniform_real_distribution<float> scaleDist(0.35f, 0.55f);
  std::uniform_real_distribution<float> angleDist(0.0f, 2.0f * M_PI);
  std::uniform_real_distribution<float> transDist(
      -0.6f, 0.6f); // 3D translation limits

  std::vector<glm::mat4> transformationMats;
  transformationMats.reserve(numTransforms);

  for (uint32_t i = 0; i < numTransforms; ++i) {
    float sX = scaleDist(rng);
    float sY = scaleDist(rng);
    float sZ = scaleDist(rng); // Add Z scaling

    // Random angles for all 3 axes
    float ax = angleDist(rng);
    float ay = angleDist(rng);
    float az = angleDist(rng);

    // Create distinct rotation matrices for each axis
    glm::mat4 rotX =
        glm::rotate(glm::mat4(1.0f), ax, glm::vec3(1.0f, 0.0f, 0.0f));
    glm::mat4 rotY =
        glm::rotate(glm::mat4(1.0f), ay, glm::vec3(0.0f, 1.0f, 0.0f));
    glm::mat4 rotZ =
        glm::rotate(glm::mat4(1.0f), az, glm::vec3(0.0f, 0.0f, 1.0f));

    // Combine rotations: R = Z * Y * X
    glm::mat4 combinedRotation = rotZ * rotY * rotX;

    // Apply scaling directly to the directional basis vectors
    // (columns 0, 1, 2)
    combinedRotation[0] = combinedRotation[0] * sX;
    combinedRotation[1] = combinedRotation[1] * sY;
    combinedRotation[2] = combinedRotation[2] * sZ;

    // Translation
    combinedRotation[3][0] = transDist(rng);
    combinedRotation[3][1] = transDist(rng);
    combinedRotation[3][2] = transDist(rng);

    combinedRotation[3][3] = 1.0f;

    transformationMats.push_back(combinedRotation);
  }
  return transformationMats;
}

} // namespace app
