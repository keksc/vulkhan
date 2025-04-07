#include "input.hpp"
#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <fmt/core.h>
#include <fmt/format.h>
#include <glm/gtc/quaternion.hpp>

#include <limits>
#include <thread>

#include "entity.hpp"

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
  if (key == GLFW_KEY_SPACE && action == GLFW_PRESS) {
    Entity &player = context->entities[0];
    player.rigidBody.velocity.y -= 1.f;
    /*if (player.rigidBody.isJumping)
      return;
    player.rigidBody.isJumping = true;
    player.rigidBody.velocity.y = player.rigidBody.jumpVelocity;*/
  }
}
static void cursorPositionCallback(GLFWwindow *window, double xpos,
                                   double ypos) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->input.cursorPos.x = static_cast<int>(xpos);
  context->input.cursorPos.y = static_cast<int>(ypos);
  for (auto &callback :
       context->inputCallbackSystems[context->currentInputCallbackSystemIndex]
           .cursorPosition)
    callback(xpos, ypos);
}
int scroll{};
void scrollCallback(GLFWwindow *window, double xoffset, double yoffset) {
  scroll += static_cast<int>(yoffset);
}
void animateSword(EngineContext &context) {
  Entity &sword = context.entities[1];
  for (float i = 0.0; i < 0.5; i += 0.01) {
    sword.transform.orientation =
        glm::angleAxis(glm::pi<float>() * i, glm::vec3(0.0f, 0.0f, 1.0f));
    std::this_thread::sleep_for(std::chrono::milliseconds(2));
  }
}
void mouseButtonCallback(GLFWwindow *window, int button, int action, int mods) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  if (button == GLFW_MOUSE_BUTTON_LEFT && action == GLFW_PRESS) {
    std::thread(animateSword, std::ref(*context)).detach();
  }
  if (button == GLFW_MOUSE_BUTTON_RIGHT) {
  }
  for (auto &callback :
       context->inputCallbackSystems[context->currentInputCallbackSystemIndex]
           .mouseButton)
    callback(button, action, mods);
}
void charCallback(GLFWwindow *window, unsigned int codepoint) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  for (auto &callback :
       context->inputCallbackSystems[context->currentInputCallbackSystemIndex]
           .character)
    callback(codepoint);
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
  Entity &player = context.entities[0];
  glm::mat3 rotationMatrix = glm::mat3(
      player.transform
          .orientation); // glm::eulerAngles(player.transform.orientation);
  glm::vec3 rotation =
      glm::vec3{-context.input.cursorPos.y, context.input.cursorPos.x, 0.f} *
      0.001f;

  rotation.y = glm::mod(rotation.y, glm::two_pi<float>());
  rotation.x = glm::clamp(rotation.x, -1.5f, 1.5f);

  // player.transform.orientation = glm::quat(rotation);
  float yaw = rotation.y;
  const glm::vec3 forwardDir{-sin(yaw), 0.f, -cos(yaw)};
  const glm::vec3 rightDir{forwardDir.z, 0.f, -forwardDir.x};
  const glm::vec3 upDir{0.f, 1.f, 0.f};

  glm::vec3 moveDir{0.f};
  if (glfwGetKey(context.window, GLFW_KEY_W))
    moveDir += forwardDir;
  if (glfwGetKey(context.window, GLFW_KEY_S))
    moveDir -= forwardDir;
  if (glfwGetKey(context.window, GLFW_KEY_D))
    moveDir += rightDir;
  if (glfwGetKey(context.window, GLFW_KEY_A))
    moveDir -= rightDir;
  if (glfwGetKey(context.window, GLFW_KEY_SPACE))
    moveDir += upDir;
  if (glfwGetKey(context.window, GLFW_KEY_LEFT_CONTROL))
    moveDir -= upDir;

  if (glm::dot(moveDir, moveDir) > std::numeric_limits<float>::epsilon()) {
    float sprint = glfwGetKey(context.window, GLFW_KEY_LEFT_SHIFT) ? 4.0 : 1.0;
    player.transform.position +=
        sprint * moveSpeed * context.frameInfo.dt * glm::normalize(moveDir);
  }
  glm::quat rotationQuat = glm::quat(rotation);
  player.transform.orientation = rotationQuat;
}
} // namespace input
} // namespace vkh
