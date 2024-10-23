#include "controller.hpp"

#include <GLFW/glfw3.h>
#include <fmt/core.h>

#include <limits>

namespace vkh {
Controller::KeyStatuses keys{};
void key_callback(GLFWwindow *window, int key, int scancode, int action,
                  int mods) {
  if (key == GLFW_KEY_A)
    keys.moveLeft = static_cast<bool>(action);
  if (key == GLFW_KEY_D)
    keys.moveRight = static_cast<bool>(action);
  if (key == GLFW_KEY_W)
    keys.moveForward = static_cast<bool>(action);
  if (key == GLFW_KEY_S)
    keys.moveBackward = static_cast<bool>(action);
  if (key == GLFW_KEY_SPACE)
    keys.moveUp = static_cast<bool>(action);
  if (key == GLFW_KEY_LEFT_CONTROL)
    keys.moveDown = static_cast<bool>(action);
  if (key == GLFW_KEY_ESCAPE)
    glfwSetWindowShouldClose(window, true);
  if (key == GLFW_KEY_O && action == GLFW_PRESS) {
    auto context =
        reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
    int cursorSetting = glfwGetInputMode(context->window, GLFW_CURSOR);
    if(cursorSetting == GLFW_CURSOR_DISABLED) cursorSetting = GLFW_CURSOR_NORMAL; else cursorSetting = GLFW_CURSOR_DISABLED;
    glfwSetInputMode(
        context->window, GLFW_CURSOR, cursorSetting);
  }

  if (key == GLFW_KEY_F11 && action == GLFW_PRESS) {
    auto context =
        reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
    if (context->window.fullscreen) {
      glfwSetWindowMonitor(window, NULL, 100, 100, 800, 600, 0);
    } else {
      GLFWmonitor *monitor = glfwGetPrimaryMonitor();
      const GLFWvidmode *mode = glfwGetVideoMode(monitor);
      glfwSetWindowMonitor(window, monitor, 0, 0, mode->width, mode->height,
                           mode->refreshRate);
    }
    context->window.fullscreen = !context->window.fullscreen;
  }
  if (key == GLFW_KEY_F3)
    keys.showDebugInfo = static_cast<bool>(action);
}
glm::vec2 mousePos;
static void cursor_position_callback(GLFWwindow *window, double xpos,
                                     double ypos) {
  mousePos.x = static_cast<float>(xpos);
  mousePos.y = static_cast<float>(ypos);
}
int scroll{};
void scroll_callback(GLFWwindow *window, double xoffset, double yoffset) {
  scroll += static_cast<int>(yoffset);
}
Controller::Controller(EngineContext &context) : context{context} {
  glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetInputMode(context.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  glfwSetKeyCallback(context.window, key_callback);
  glfwSetCursorPosCallback(context.window, cursor_position_callback);
  glfwSetScrollCallback(context.window, scroll_callback);
}

void Controller::moveInPlaneXZ(EngineContext &context, float dt) {
  auto &cameraRotation =
      context.camera.rotation;
  glm::vec3 rotate{-mousePos.y, mousePos.x, 0.f};
  cameraRotation = rotate * 0.0007f;

  if (scroll) {
    context.camera.position.y -= scroll * 0.02;
    scroll = 0;
  }

  cameraRotation.y = glm::mod(cameraRotation.y, glm::two_pi<float>());
  cameraRotation.x = glm::clamp(cameraRotation.x, -1.5f, 1.5f);

  float yaw = context.camera.rotation.y;
  const glm::vec3 forwardDir{sin(yaw), 0.f, cos(yaw)};
  const glm::vec3 rightDir{forwardDir.z, 0.f, -forwardDir.x};
  const glm::vec3 upDir{0.f, -1.f, 0.f};

  glm::vec3 moveDir{0.f};
  if (keys.moveForward)
    moveDir += forwardDir;
  if (keys.moveBackward)
    moveDir -= forwardDir;
  if (keys.moveRight)
    moveDir += rightDir;
  if (keys.moveLeft)
    moveDir -= rightDir;
  if (keys.moveUp)
    moveDir += upDir;
  if (keys.moveDown)
    moveDir -= upDir;

  if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
    context.camera.position +=
        moveSpeed * dt * glm::normalize(moveDir);
  }
}
} // namespace vkh
