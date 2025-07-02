#include "input.hpp"
#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <glm/ext/vector_common.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtx/norm.hpp>

#include <limits>

namespace vkh {
namespace input {
float moveSpeed{5.f};
float lookSpeed{1.5f};
void keyCallback(GLFWwindow *window, int key, int scancode, int action,
                 int mods) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  for (auto &pair :
       context->inputCallbackSystems[context->currentInputCallbackSystemKey]
           .key)
    pair.second(key, scancode, action, mods);
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
  for (auto &pair :
       context->inputCallbackSystems[context->currentInputCallbackSystemKey]
           .cursorPosition)
    pair.second(xpos, ypos);
}
int scroll{};
void scrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
  scroll += static_cast<int>(yoffset);
}
const int doubleClickDelay = 300;
void doubleClickCallback(GLFWwindow *window) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  for (auto &pair :
       context->inputCallbackSystems[context->currentInputCallbackSystemKey]
           .doubleClick)
    pair.second();
}
void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
  static std::chrono::high_resolution_clock::time_point lastClick{};

  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    if (std::chrono::high_resolution_clock::now() - lastClick <=
        std::chrono::milliseconds(doubleClickDelay))
      doubleClickCallback(window);
    lastClick = std::chrono::high_resolution_clock::now();
  }

  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  for (auto &pair :
       context->inputCallbackSystems[context->currentInputCallbackSystemKey]
           .mouseButton)
    pair.second(button, action, mods);
}
void charCallback(GLFWwindow *window, unsigned int codepoint) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (!context->currentInputCallbackSystemKey)
    return;
  for (auto &pair :
       context->inputCallbackSystems[context->currentInputCallbackSystemKey]
           .character)
    pair.second(codepoint);
}
void init(EngineContext &context) {
  glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetInputMode(context.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  glfwSetKeyCallback(context.window, keyCallback);
  glfwSetCharCallback(context.window, charCallback);
  glfwSetCursorPosCallback(context.window, cursorPositionCallback);
  glfwSetScrollCallback(context.window, scrollCallback);
  glfwSetMouseButtonCallback(context.window, mouseButtonCallback);
}
glm::dvec2 lastPos;
void moveInPlaneXZ(EngineContext &context) {
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

  if (glfwGetKey(context.window, GLFW_KEY_W) == GLFW_PRESS)
    moveDir += forward;
  if (glfwGetKey(context.window, GLFW_KEY_S) == GLFW_PRESS)
    moveDir -= forward;
  if (glfwGetKey(context.window, GLFW_KEY_D) == GLFW_PRESS)
    moveDir += rightDir;
  if (glfwGetKey(context.window, GLFW_KEY_A) == GLFW_PRESS)
    moveDir -= rightDir;
  if (glfwGetKey(context.window, GLFW_KEY_SPACE) == GLFW_PRESS)
    moveDir += up;
  if (glfwGetKey(context.window, GLFW_KEY_LEFT_CONTROL) == GLFW_PRESS)
    moveDir -= up;

  if (glm::length2(moveDir) > std::numeric_limits<float>::epsilon()) {
    float sprint =
        (glfwGetKey(context.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 30.0f
                                                                        : 2.0f;
    context.camera.position +=
        glm::normalize(moveDir) * sprint * moveSpeed * context.frameInfo.dt;
  }
}
} // namespace input
} // namespace vkh
