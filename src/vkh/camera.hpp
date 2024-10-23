#pragma once

// libs
#include <glm/fwd.hpp>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>

namespace vkh {

class Camera {
public:
  Camera(glm::vec3 position = glm::vec3(0.f),
         glm::vec3 rotation = glm::vec3(0.f));
  void calcOrthographicProjection(float left, float right, float top,
                                  float bottom, float near, float far);
  void calcPerspectiveProjection(float fovy, float aspect, float near,
                                 float far);

  void calcViewDirection(glm::vec3 direction,
                         glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
  void calcViewTarget(glm::vec3 target,
                      glm::vec3 up = glm::vec3{0.f, -1.f, 0.f});
  void calcViewYXZ();

  const glm::mat4 &getProjection() const { return projectionMatrix; }
  const glm::mat4 &getView() const { return viewMatrix; }
  const glm::mat4 &getInverseView() const { return inverseViewMatrix; }
  const glm::vec3 getPosition() const {
    return glm::vec3(inverseViewMatrix[3]);
  }

  glm::vec3 position;
  glm::vec3 rotation;

private:
  glm::mat4 projectionMatrix{1.f};
  glm::mat4 viewMatrix{1.f};
  glm::mat4 inverseViewMatrix{1.f};
};
} // namespace vkh
