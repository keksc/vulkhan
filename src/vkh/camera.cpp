#include "camera.hpp"

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include "engineContext.hpp"

namespace vkh {

namespace camera {
void calcViewDirection(EngineContext &context, glm::vec3 direction,
                       glm::vec3 up) {
  const glm::vec3 w{glm::normalize(direction)};
  const glm::vec3 u{glm::normalize(glm::cross(w, up))};
  const glm::vec3 v{glm::cross(w, u)};

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

void calcViewTarget(EngineContext &context, glm::vec3 target, glm::vec3 up) {
  calcViewDirection(context, target - context.camera.position, up);
}

void calcViewYXZ(EngineContext &context) {
  const glm::quat &q = context.camera.orientation;
  const glm::vec3 &p = context.camera.position;

  // Inverse rotation (conjugate of unit quaternion)
  glm::quat invRot = glm::conjugate(q);

  // View matrix is the inverse of the camera's world transform
  // R^T * T^-1 (rotation transpose * translation inverse)
  context.camera.viewMatrix = glm::mat4_cast(invRot);
  context.camera.viewMatrix[3] =
      glm::vec4(-glm::vec3(context.camera.viewMatrix[0]) * p.x -
                    glm::vec3(context.camera.viewMatrix[1]) * p.y -
                    glm::vec3(context.camera.viewMatrix[2]) * p.z,
                1.0f);

  // Inverse view matrix is camera transform: T * R
  context.camera.inverseViewMatrix = glm::mat4_cast(q);
  context.camera.inverseViewMatrix[3] = glm::vec4(p, 1.0f);
}
} // namespace camera
} // namespace vkh
