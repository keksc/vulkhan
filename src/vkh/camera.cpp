#include "camera.hpp"

#include <cassert>
#include <limits>

#include "engineContext.hpp"

namespace vkh {

namespace camera {
void calcPerspectiveProjection(EngineContext &context, float fovy, float aspect,
                               float near, float far) {
  assert(glm::abs(aspect - std::numeric_limits<float>::epsilon()) > 0.0f);
  const float tanHalfFovy = tan(fovy / 2.f);
  context.camera.projectionMatrix = glm::mat4{0.0f};
  context.camera.projectionMatrix[0][0] =
      1.f / (aspect * tanHalfFovy);
  context.camera.projectionMatrix[1][1] = 1.f / (tanHalfFovy);
  context.camera.projectionMatrix[2][2] = far / (far - near);
  context.camera.projectionMatrix[2][3] = 1.f;
  context.camera.projectionMatrix[3][2] = -(far * near) / (far - near);
}

void calcViewDirection(EngineContext &context, glm::vec3 direction,
                       glm::vec3 up) {
  const glm::vec3 w{glm::normalize(direction)};
  const glm::vec3 u{glm::normalize(glm::cross(w, up))};
  const glm::vec3 v{glm::cross(w, u)};

  /*context.camera.viewMatrix = glm::mat4{1.f};
  context.camera.viewMatrix[0][0] = u.x;
  context.camera.viewMatrix[1][0] = u.y;
  context.camera.viewMatrix[2][0] = u.z;
  context.camera.viewMatrix[0][1] = v.x;
  context.camera.viewMatrix[1][1] = v.y;
  context.camera.viewMatrix[2][1] = v.z;
  context.camera.viewMatrix[0][2] = w.x;
  context.camera.viewMatrix[1][2] = w.y;
  context.camera.viewMatrix[2][2] = w.z;
  context.camera.viewMatrix[3][0] = -glm::dot(u, context.camera.position);
  context.camera.viewMatrix[3][1] = -glm::dot(v, context.camera.position);
  context.camera.viewMatrix[3][2] = -glm::dot(w, context.camera.position);

  context.camera.inverseViewMatrix = glm::mat4{1.f};
  context.camera.inverseViewMatrix[0][0] = u.x;
  context.camera.inverseViewMatrix[0][1] = u.y;
  context.camera.inverseViewMatrix[0][2] = u.z;
  context.camera.inverseViewMatrix[1][0] = v.x;
  context.camera.inverseViewMatrix[1][1] = v.y;
  context.camera.inverseViewMatrix[1][2] = v.z;
  context.camera.inverseViewMatrix[2][0] = w.x;
  context.camera.inverseViewMatrix[2][1] = w.y;
  context.camera.inverseViewMatrix[2][2] = w.z;
  context.camera.inverseViewMatrix[3][0] = context.camera.position.x;
  context.camera.inverseViewMatrix[3][1] = context.camera.position.y;
  context.camera.inverseViewMatrix[3][2] = context.camera.position.z;*/
}

void calcViewTarget(EngineContext &context, glm::vec3 target, glm::vec3 up) {
  calcViewDirection(context, target - context.camera.position, up);
}

void calcViewYXZ(EngineContext &context) {
  const float c3 = glm::cos(context.camera.rotation.z);
  const float s3 = glm::sin(context.camera.rotation.z);
  const float c2 = glm::cos(context.camera.rotation.x);
  const float s2 = glm::sin(context.camera.rotation.x);
  const float c1 = glm::cos(context.camera.rotation.y);
  const float s1 = glm::sin(context.camera.rotation.y);
  const glm::vec3 u{(c1 * c3 + s1 * s2 * s3), (c2 * s3),
                    (c1 * s2 * s3 - c3 * s1)};
  const glm::vec3 v{(c3 * s1 * s2 - c1 * s3), (c2 * c3),
                    (c1 * c3 * s2 + s1 * s3)};
  const glm::vec3 w{(c2 * s1), (-s2), (c1 * c2)};
  context.camera.viewMatrix = glm::mat4{1.f};
  context.camera.viewMatrix[0][0] = u.x;
  context.camera.viewMatrix[1][0] = u.y;
  context.camera.viewMatrix[2][0] = u.z;
  context.camera.viewMatrix[0][1] = v.x;
  context.camera.viewMatrix[1][1] = v.y;
  context.camera.viewMatrix[2][1] = v.z;
  context.camera.viewMatrix[0][2] = w.x;
  context.camera.viewMatrix[1][2] = w.y;
  context.camera.viewMatrix[2][2] = w.z;
  context.camera.viewMatrix[3][0] = -glm::dot(u, context.camera.position);
  context.camera.viewMatrix[3][1] = -glm::dot(v, context.camera.position);
  context.camera.viewMatrix[3][2] = -glm::dot(w, context.camera.position);

  context.camera.inverseViewMatrix = glm::mat4{1.f};
  context.camera.inverseViewMatrix[0][0] = u.x;
  context.camera.inverseViewMatrix[0][1] = u.y;
  context.camera.inverseViewMatrix[0][2] = u.z;
  context.camera.inverseViewMatrix[1][0] = v.x;
  context.camera.inverseViewMatrix[1][1] = v.y;
  context.camera.inverseViewMatrix[1][2] = v.z;
  context.camera.inverseViewMatrix[2][0] = w.x;
  context.camera.inverseViewMatrix[2][1] = w.y;
  context.camera.inverseViewMatrix[2][2] = w.z;
  context.camera.inverseViewMatrix[3][0] = context.camera.position.x;
  context.camera.inverseViewMatrix[3][1] = context.camera.position.y;
  context.camera.inverseViewMatrix[3][2] = context.camera.position.z;
}

} // namespace camera
} // namespace vkh
