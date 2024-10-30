#include "input.hpp"
#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#include <fmt/format.h>

#include <glm/fwd.hpp>
#include <limits>

#include "entity.hpp"

namespace vkh {
namespace input {
float moveSpeed{3.f};
float lookSpeed{1.5f};
const float jumpSpeed{.5f};
void key_callback(GLFWwindow *window, int key, int scancode, int action,
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
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
    Entity &player = context->entities[0];
    if (player.rigidBody.isJumping)
      return;
    player.rigidBody.isJumping = true;
    player.rigidBody.velocity.y = player.rigidBody.jumpVelocity;
  }
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
void init(EngineContext &context) {
  glfwSetInputMode(context.window, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
  glfwSetInputMode(context.window, GLFW_RAW_MOUSE_MOTION, GLFW_TRUE);
  glfwSetKeyCallback(context.window, key_callback);
  glfwSetCursorPosCallback(context.window, cursor_position_callback);
  glfwSetScrollCallback(context.window, scroll_callback);
}

void moveInPlaneXZ(EngineContext &context) {
  Entity &viewerEntity = context.entities[0];
  glm::vec3 rotate{-mousePos.y, mousePos.x, 0.f};
  viewerEntity.transform.rotation = rotate * 0.0007f;

  if (scroll) {
    viewerEntity.transform.translation.y -= scroll * 0.02;
    scroll = 0;
  }

  viewerEntity.transform.rotation.y =
      glm::mod(viewerEntity.transform.rotation.y, glm::two_pi<float>());
  viewerEntity.transform.rotation.x =
      glm::clamp(viewerEntity.transform.rotation.x, -1.5f, 1.5f);

  float yaw = viewerEntity.transform.rotation.y;
  const glm::vec3 forwardDir{sin(yaw), 0.f, cos(yaw)};
  const glm::vec3 rightDir{forwardDir.z, 0.f, -forwardDir.x};
  const glm::vec3 upDir{0.f, -1.f, 0.f};

  glm::vec3 moveDir{0.f};
  if (glfwGetKey(context.window, GLFW_KEY_W))
    moveDir += forwardDir;
  if (glfwGetKey(context.window, GLFW_KEY_S))
    moveDir -= forwardDir;
  if (glfwGetKey(context.window, GLFW_KEY_D))
    moveDir += rightDir;
  if (glfwGetKey(context.window, GLFW_KEY_A))
    moveDir -= rightDir;
  // if (glfwGetKey(context.window, GLFW_KEY_SPACE))
  //   moveDir += upDir;
  if (glfwGetKey(context.window, GLFW_KEY_LEFT_CONTROL))
    moveDir -= upDir;

  if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
    viewerEntity.transform.translation +=
        moveSpeed * context.frameInfo.dt * glm::normalize(moveDir);
  }
  if (viewerEntity.rigidBody.isJumping) {
    viewerEntity.rigidBody.acceleration.y = 9.81f;
    viewerEntity.rigidBody.velocity +=
        viewerEntity.rigidBody.acceleration * context.frameInfo.dt;
    viewerEntity.transform.translation +=
        viewerEntity.rigidBody.velocity * jumpSpeed * context.frameInfo.dt;
  }
  if (viewerEntity.transform.translation.y >= GROUND_LEVEL && viewerEntity.rigidBody.isJumping) {
    viewerEntity.rigidBody.isJumping = false;
    viewerEntity.transform.translation.y = GROUND_LEVEL;
  }
}
} // namespace input
} // namespace vkh
