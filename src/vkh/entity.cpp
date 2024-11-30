#include "entity.hpp"
#include <glm/ext/matrix_transform.hpp>

namespace vkh {

glm::mat4 Transform::mat4() {
  /*const float c1 = glm::cos(rotation.y);
  const float c2 = glm::cos(rotation.x);
  const float c3 = glm::cos(rotation.z);
  const float s1 = glm::sin(rotation.y);
  const float s2 = glm::sin(rotation.x);
  const float s3 = glm::sin(rotation.z);
  return glm::mat4{
      {
          scale.x * (c1 * c3 + s1 * s2 * s3),
          scale.x * (c2 * s3),
          scale.x * (c1 * s2 * s3 - c3 * s1),
          0.0f,
      },
      {
          scale.y * (c3 * s1 * s2 - c1 * s3),
          scale.y * (c2 * c3),
          scale.y * (c1 * c3 * s2 + s1 * s3),
          0.0f,
      },
      {
          scale.z * (c2 * s1),
          scale.z * (-s2),
          scale.z * (c1 * c2),
          0.0f,
      },
      {position.x, position.y, position.z, 1.0f} };*/
  /*glm::mat4 rotationMatrix = glm::mat4(orientation);
  glm::mat4 scaleMatrix = glm::scale(glm::mat4(1.f), scale);
  glm::mat4 translationMatrix = glm::translate(glm::mat4(1.f), position);
  return translationMatrix * rotationMatrix * scaleMatrix;*/
  glm::quat q = glm::normalize(orientation);

  // Rotation columns using quaternion
  glm::vec3 right = q * glm::vec3(1.0f, 0.0f, 0.0f);   // X-axis rotation
  glm::vec3 up = q * glm::vec3(0.0f, 1.0f, 0.0f);      // Y-axis rotation
  glm::vec3 forward = q * glm::vec3(0.0f, 0.0f, 1.0f); // Z-axis rotation

  // Apply scaling to the rotation
  right *= scale.x;
  up *= scale.y;
  forward *= scale.z;

  // Construct the transformation matrix manually
  glm::mat4 transform(1.0f);
  transform[0] = glm::vec4(right, 0.0f);
  transform[1] = glm::vec4(up, 0.0f);
  transform[2] = glm::vec4(forward, 0.0f);
  transform[3] = glm::vec4(position, 1.0f);
  return transform;
}

glm::mat3 Transform::normalMatrix() {
  /*const float c1 = glm::cos(rotation.y);
  const float c2 = glm::cos(rotation.x);
  const float c3 = glm::cos(rotation.z);
  const float s1 = glm::sin(rotation.y);
  const float s2 = glm::sin(rotation.x);
  const float s3 = glm::sin(rotation.z);
  const glm::vec3 invScale = 1.0f / scale;

  return glm::mat3{
      {
          invScale.x * (c1 * c3 + s1 * s2 * s3),
          invScale.x * (c2 * s3),
          invScale.x * (c1 * s2 * s3 - c3 * s1),
      },
      {
          invScale.y * (c3 * s1 * s2 - c1 * s3),
          invScale.y * (c2 * c3),
          invScale.y * (c1 * c3 * s2 + s1 * s3),
      },
      {
          invScale.z * (c2 * s1),
          invScale.z * (-s2),
          invScale.z * (c1 * c2),
      },
  };*/
  glm::mat3 rotationMatrix = glm::mat3(orientation);
  glm::mat3 scaleMatrix =
      glm::mat3(1.0f / scale.x, 0.0f, 0.0f, 0.0f, 1.0f / scale.y, 0.0f, 0.0f,
                0.0f, 1.0f / scale.z);
  return rotationMatrix * scaleMatrix;
}
} // namespace vkh
