#include "input.hpp"
#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#include <fmt/format.h>
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
  if (key == GLFW_KEY_O && action == GLFW_PRESS) {
    int cursorSetting = glfwGetInputMode(context->window, GLFW_CURSOR);
    if (cursorSetting == GLFW_CURSOR_DISABLED)
      cursorSetting = GLFW_CURSOR_NORMAL;
    else
      cursorSetting = GLFW_CURSOR_DISABLED;
    glfwSetInputMode(context->window, GLFW_CURSOR, cursorSetting);
    return;
  }
  if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
    if (context->window.fullscreen) {
      glfwSetWindowMonitor(window, NULL, 100, 100, 800, 600, 0);
    } else {
      GLFWmonitor *monitor = glfwGetPrimaryMonitor();
      const GLFWvidmode *mode = glfwGetVideoMode(monitor);
      glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                           mode->refreshRate);
    }
    context->window.fullscreen = !context->window.fullscreen;
    return;
  }
  if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
    glfwSetWindowShouldClose(context->window, GLFW_TRUE);
  }
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
  context->input.cursorPos.x = static_cast<int>(xpos);
  context->input.cursorPos.y = static_cast<int>(ypos);
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
void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  // if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
  //   std::thread(animateSword, std::ref(*context)).detach();
  // }
  if (button == GLFW_MOUSE_BUTTON_RIGHT) {
  }
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
void moveInPlaneXZ(EngineContext &context) {
  static glm::vec2 lastCursorPos = context.input.cursorPos;
  glm::vec2 currentCursorPos = context.input.cursorPos;

  glm::vec2 delta = (currentCursorPos - lastCursorPos) * 0.001f;
  lastCursorPos = currentCursorPos;

  static float yaw = 0.0f;
  static float pitch = 0.0f;

  yaw += delta.x;
  pitch -= delta.y;

  pitch = glm::clamp(pitch, -glm::half_pi<float>() + 0.01f,
                     glm::half_pi<float>() - 0.01f);

  glm::quat q_yaw = glm::angleAxis(yaw, glm::vec3(0.0f, 1.0f, 0.0f));

  glm::quat q_pitch = glm::angleAxis(pitch, glm::vec3(1.0f, 0.0f, 0.0f));

  context.camera.orientation = q_yaw * q_pitch;

  glm::vec3 forward = context.camera.orientation *
                      glm::vec3(0.0f, 0.0f, -1.0f);
  glm::vec3 right = context.camera.orientation *
                    glm::vec3(-1.0f, 0.0f, 0.0f);
  glm::vec3 up = context.camera.orientation *
                 glm::vec3(0.0f, 1.0f, 0.0f);

  glm::vec3 moveDir{0.0f};
  if (glfwGetKey(context.window, GLFW_KEY_W))
    moveDir += forward;
  if (glfwGetKey(context.window, GLFW_KEY_S))
    moveDir -= forward;
  if (glfwGetKey(context.window, GLFW_KEY_D))
    moveDir += right;
  if (glfwGetKey(context.window, GLFW_KEY_A))
    moveDir -= right;
  if (glfwGetKey(context.window, GLFW_KEY_SPACE))
    moveDir += up;
  if (glfwGetKey(context.window, GLFW_KEY_LEFT_CONTROL))
    moveDir -= up;

  if (glm::length2(moveDir) > std::numeric_limits<float>::epsilon()) {
    float sprint =
        glfwGetKey(context.window, GLFW_KEY_LEFT_SHIFT) ? 4.0f : 1.0f;
    context.camera.position +=
        glm::normalize(moveDir) * sprint * moveSpeed * context.frameInfo.dt;
  }
}
} // namespace input
} // namespace vkh
