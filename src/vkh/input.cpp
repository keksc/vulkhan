#include "input.hpp"
#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <glm/ext/vector_common.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#include <limits>

#include "AxisAlignedBoundingBox.hpp"
#include "systems/entity/entities.hpp"

namespace vkh {
namespace input {
std::unordered_map<Action, unsigned int> keybinds;
float moveSpeed{5.f};
float lookSpeed{1.5f};

AABB getEntityAABB(const EntitySys::Entity &entity) {
  // Get mesh's model-space AABB
  const auto &mesh = entity.scene->meshes[entity.meshIndex];
  glm::vec3 modelMin = mesh.aabb.min;
  glm::vec3 modelMax = mesh.aabb.max;

  // Use full transform: entity.transform.mat4() * mesh.transform
  glm::mat4 fullTransform = entity.transform.mat4() * mesh.transform;

  // Transform AABB to world space
  glm::vec3 corners[8] = {{modelMin.x, modelMin.y, modelMin.z},
                          {modelMax.x, modelMin.y, modelMin.z},
                          {modelMin.x, modelMax.y, modelMin.z},
                          {modelMax.x, modelMax.y, modelMin.z},
                          {modelMin.x, modelMin.y, modelMax.z},
                          {modelMax.x, modelMin.y, modelMax.z},
                          {modelMin.x, modelMax.y, modelMax.z},
                          {modelMax.x, modelMax.y, modelMax.z}};

  glm::vec3 worldMin{std::numeric_limits<float>::max()};
  glm::vec3 worldMax{-std::numeric_limits<float>::max()};

  for (const auto &corner : corners) {
    glm::vec4 worldCorner = fullTransform * glm::vec4(corner, 1.0f);
    worldMin = glm::min(worldMin, glm::vec3(worldCorner));
    worldMax = glm::max(worldMax, glm::vec3(worldCorner));
  }

  return {worldMin, worldMax};
}

void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                 int mods) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  context->inputCallbackSystems[context->currentInputCallbackSystemKey].key(
      key, scancode, action, mods);
}

void cursorPositionCallback(GLFWwindow *window, double xpos, double ypos) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->input.cursorPos = glm::vec2{xpos, ypos} /
                                 static_cast<glm::vec2>(context->window.size) *
                                 2.f -
                             1.f;
  if (!context->currentInputCallbackSystemKey)
    return;
  context->inputCallbackSystems[context->currentInputCallbackSystemKey]
      .cursorPosition(xpos, ypos);
}

int scroll{};
void scrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
  scroll += static_cast<int>(yoffset);
}

void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
  static std::chrono::high_resolution_clock::time_point lastClick{};

  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  context->inputCallbackSystems[context->currentInputCallbackSystemKey]
      .mouseButton(button, action, mods);
}

void charCallback(GLFWwindow *window, unsigned int codepoint) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  context->inputCallbackSystems[context->currentInputCallbackSystemKey]
      .character(codepoint);
}

void dropCallback(GLFWwindow *window, int count, const char **paths) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  context->inputCallbackSystems[context->currentInputCallbackSystemKey].drop(
      count, paths);
}

void init(EngineContext &context) {
  glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetInputMode(context.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  glfwSetKeyCallback(context.window, keyCallback);
  glfwSetCharCallback(context.window, charCallback);
  glfwSetCursorPosCallback(context.window, cursorPositionCallback);
  glfwSetScrollCallback(context.window, scrollCallback);
  glfwSetMouseButtonCallback(context.window, mouseButtonCallback);
  glfwSetDropCallback(context.window, dropCallback);

  keybinds[MoveForward] = GLFW_KEY_W;
  keybinds[MoveBackward] = GLFW_KEY_S;
  keybinds[MoveLeft] = GLFW_KEY_A;
  keybinds[MoveRight] = GLFW_KEY_D;
  keybinds[PlaceRect] = GLFW_KEY_R;
  keybinds[PlaceText] = GLFW_KEY_T;
  keybinds[PlaceLine] = GLFW_KEY_L;
}

glm::dvec2 lastPos;
void update(EngineContext &context, EntitySys &entitySys) {
  glm::dvec2 currentPos;
  glfwGetCursorPos(context.window, &currentPos.x, &currentPos.y);

  glm::dvec2 delta = currentPos - lastPos;
  lastPos = currentPos;

  const float sensitivity = .002f;
  double dYaw = delta.x * sensitivity;
  double dPitch = -delta.y * sensitivity;

  context.camera.yaw += dYaw;
  context.camera.pitch += dPitch;

  const float maxPitch = glm::radians(89.f);
  context.camera.pitch = glm::clamp(context.camera.pitch, -maxPitch, maxPitch);

  context.camera.yaw = glm::mod(context.camera.yaw, glm::two_pi<float>());

  glm::quat yawQuat =
      glm::angleAxis(context.camera.yaw, glm::vec3(0.f, 1.f, 0.f));
  glm::vec3 localRight = yawQuat * glm::vec3(1.f, 0.f, 0.f);
  glm::quat pitchQuat = glm::angleAxis(context.camera.pitch, localRight);
  context.camera.orientation = pitchQuat * yawQuat;

  glm::vec3 forward = context.camera.orientation * glm::vec3(0.f, 0.f, -1.f);
  glm::vec3 rightDir = context.camera.orientation * glm::vec3(-1.f, 0.f, 0.f);
  glm::vec3 up = glm::vec3(0.f, 1.f, 0.f);
  glm::vec3 moveDir{};

  if (glfwGetKey(context.window, keybinds[MoveForward]) == GLFW_PRESS)
    moveDir += forward;
  if (glfwGetKey(context.window, keybinds[MoveBackward]) == GLFW_PRESS)
    moveDir -= forward;
  if (glfwGetKey(context.window, keybinds[MoveRight]) == GLFW_PRESS)
    moveDir += rightDir;
  if (glfwGetKey(context.window, keybinds[MoveLeft]) == GLFW_PRESS)
    moveDir -= rightDir;
  if (glfwGetKey(context.window, GLFW_KEY_SPACE) == GLFW_PRESS)
    moveDir += up;
  if (glfwGetKey(context.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    moveDir -= up;

  if (glm::length2(moveDir) > std::numeric_limits<float>::epsilon()) {
    float sprint =
        (glfwGetKey(context.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 30.0f
                                                                        : 2.0f;
    glm::vec3 velocity =
        glm::normalize(moveDir) * sprint * moveSpeed * context.frameInfo.dt;
    glm::vec3 newPosition = context.camera.position + velocity;

    // Camera AABB (small box around camera position)
    const float cameraSize = 0.2f;
    AABB cameraAABB{newPosition - glm::vec3(cameraSize / 2.0f),
                    newPosition + glm::vec3(cameraSize / 2.0f)};

    // Check collisions with all entities
    bool collision = false;
    // for (const auto &entity : entitySys.entities) {
    //   AABB entityAABB = getEntityAABB(entity);
    //   if (cameraAABB.intersects(entityAABB)) {
    //     collision = true;
    //     break;
    //   }
    // }
    collision = false;

    // Only update position if no collision
    // if (!collision) {CMakeFiles/vulkhan.dir/src/main.cpp.o
      context.camera.position = newPosition;
    // } else {
    //   // Try sliding along each axis
    //   glm::vec3 testPosition = context.camera.position;
    //   glm::vec3 axisVelocity[3] = {
    //       velocity * glm::vec3(1.f, 0.f, 0.f), // X component
    //       velocity * glm::vec3(0.f, 1.f, 0.f), // Y component
    //       velocity * glm::vec3(0.f, 0.f, 1.f)  // Z component
    //   };
    //
    //   for (const auto &axisVel : axisVelocity) {
    //     if (glm::length2(axisVel) > std::numeric_limits<float>::epsilon()) {
    //       glm::vec3 axisTestPosition = testPosition + axisVel;
    //       AABB axisCameraAABB{axisTestPosition - glm::vec3(cameraSize / 2.0f),
    //                           axisTestPosition + glm::vec3(cameraSize / 2.0f)};
    //       bool axisCollision = false;
    //       for (const auto &entity : entitySys.entities) {
    //         AABB entityAABB = getEntityAABB(entity);
    //         if (axisCameraAABB.intersects(entityAABB)) {
    //           axisCollision = true;
    //           break;
    //         }
    //       }
    //       if (!axisCollision) {
    //         testPosition = axisTestPosition;
    //       }
    //     }
    //   }
    //   context.camera.position = testPosition;
    // }
  }
}
} // namespace input
} // namespace vkh
