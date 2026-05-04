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

  context.camera.inverseViewMatrix = glm::mat4_cast(q);
  context.camera.inverseViewMatrix[3] = glm::vec4(p, 1.0f);
}

Ray getPickingRay(const EngineContext &context, glm::vec2 mousePos) {
  glm::mat4 invProj = glm::inverse(context.camera.projectionMatrix);
  glm::mat4 invView = context.camera.inverseViewMatrix;

  // mousePos is in NDC [-1, 1]
  glm::vec4 rayClip = glm::vec4(mousePos.x, mousePos.y, -1.0, 1.0);
  glm::vec4 rayEye = invProj * rayClip;
  rayEye = glm::vec4(rayEye.x, rayEye.y, -1.0, 0.0);

  glm::vec3 rayWorld = glm::vec3(invView * rayEye);
  rayWorld = glm::normalize(rayWorld);

  return Ray{context.camera.position, rayWorld};
}

std::vector<glm::vec4> getFrustumPlanes(const glm::mat4 &m) {
  std::vector<glm::vec4> planes(6);
  // Left
  planes[0] = glm::vec4(m[0][3] + m[0][0], m[1][3] + m[1][0], m[2][3] + m[2][0],
                        m[3][3] + m[3][0]);
  // Right
  planes[1] = glm::vec4(m[0][3] - m[0][0], m[1][3] - m[1][0], m[2][3] - m[2][0],
                        m[3][3] - m[3][0]);
  // Bottom
  planes[2] = glm::vec4(m[0][3] + m[0][1], m[1][3] + m[1][1], m[2][3] + m[2][1],
                        m[3][3] + m[3][1]);
  // Top
  planes[3] = glm::vec4(m[0][3] - m[0][1], m[1][3] - m[1][1], m[2][3] - m[2][1],
                        m[3][3] - m[3][1]);
  // Near
  planes[4] = glm::vec4(m[0][3] + m[0][2], m[1][3] + m[1][2], m[2][3] + m[2][2],
                        m[3][3] + m[3][2]);
  // Far
  planes[5] = glm::vec4(m[0][3] - m[0][2], m[1][3] - m[1][2], m[2][3] - m[2][2],
                        m[3][3] - m[3][2]);

  for (int i = 0; i < 6; i++) {
    float length = glm::length(glm::vec3(planes[i]));
    planes[i] /= length;
  }
  return planes;
}
} // namespace camera
} // namespace vkh
