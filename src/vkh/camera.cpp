#include "camera.hpp"

#include <cassert>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/gtc/quaternion.hpp>
#include <glm/trigonometric.hpp>

#include "engineContext.hpp"

#include <limits>

namespace vkh {

namespace camera {
void calcPerspectiveProjection(EngineContext &context, float fovy, float aspect,
                               float near, float far) {
  assert(aspect > std::numeric_limits<float>::epsilon());

  const float tanHalfFovy = tan(fovy / 2.f);

  context.camera.projectionMatrix =
      glm::mat4{{1.f / (aspect * tanHalfFovy), 0.f, 0.f, 0.f},
                {0.f, 1.f / tanHalfFovy, 0.f, 0.f},
                {0.f, 0.f, far / (far - near), 1.f},
                {0.f, 0.f, -(far * near) / (far - near), 0.f}};
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
  glm::quat orientation = context.camera.orientation;

  glm::mat3 rotationMatrix = glm::mat3(orientation);

  const glm::vec3 u = rotationMatrix[0];
  const glm::vec3 v = rotationMatrix[1];
  const glm::vec3 w = rotationMatrix[2];

  context.camera.viewMatrix = glm::mat4{1.0f};

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

  context.camera.inverseViewMatrix = glm::mat4{1.0f};

  context.camera.inverseViewMatrix[0] = glm::vec4(u, 0.0f);
  context.camera.inverseViewMatrix[1] = glm::vec4(v, 0.0f);
  context.camera.inverseViewMatrix[2] = glm::vec4(w, 0.0f);

  context.camera.inverseViewMatrix[3] =
      glm::vec4(context.camera.position, 1.0f);
}
} // namespace camera
} // namespace vkh
