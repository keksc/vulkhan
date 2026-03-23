#include "input.hpp"
#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#include <chrono>
#include <glm/ext/vector_common.hpp>
#define GLM_ENABLE_EXPERIMENTAL
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtx/norm.hpp>

#include <limits>

namespace vkh {
namespace input {
std::unordered_map<Action, unsigned int> keybinds;
float moveSpeed{5.f};
float lookSpeed{1.5f};

std::string getKeyName(int key) {
  char const *str = glfwGetKeyName(key, 0);
  if (str)
    return std::string(str);
  switch (key) {
  case GLFW_KEY_SPACE:
    return "space";
  case GLFW_KEY_ESCAPE:
    return "escape";
  case GLFW_KEY_ENTER:
    return "enter";
  case GLFW_KEY_LEFT_CONTROL:
    return "left control";
  case GLFW_KEY_RIGHT_CONTROL:
    return "right control";
  case GLFW_KEY_LEFT_SHIFT:
    return "left shift";
  case GLFW_KEY_RIGHT_SHIFT:
    return "right shift";
  case GLFW_KEY_LEFT_ALT:
    return "left alt";
  case GLFW_KEY_RIGHT_ALT:
    return "right alt";
  case GLFW_KEY_LEFT_SUPER:
    return "left super";
  case GLFW_KEY_RIGHT_SUPER:
    return "right super";
  default:
    return "unknown";
  }
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
  keybinds[MoveUp] = GLFW_KEY_SPACE;
  keybinds[MoveDown] = GLFW_KEY_LEFT_CONTROL;
  keybinds[PlaceRect] = GLFW_KEY_R;
  keybinds[PlaceText] = GLFW_KEY_T;
  keybinds[PlaceLine] = GLFW_KEY_L;
}

glm::dvec2 lastPos;
void update(EngineContext &context,
            std::vector<vkh::EntitySys::Entity> &entities) {
  glm::dvec2 currentPos;
  glfwGetCursorPos(context.window, &currentPos.x, &currentPos.y);

  glm::dvec2 delta = currentPos - lastPos;
  lastPos = currentPos;

  const float sensitivity = .002f;
  double dYaw = delta.x * sensitivity;
  double dPitch = -delta.y * sensitivity;

  context.camera.yaw -= dYaw;
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
  glm::vec3 rightDir = context.camera.orientation * glm::vec3(1.f, 0.f, 0.f);
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
  if (glfwGetKey(context.window, keybinds[MoveUp]) == GLFW_PRESS)
    moveDir += up;
  if (glfwGetKey(context.window, keybinds[MoveDown]) == GLFW_PRESS)
    moveDir -= up;

  if (glm::length2(moveDir) > std::numeric_limits<float>::epsilon()) {
    float sprint =
        (glfwGetKey(context.window, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS) ? 30.0f
                                                                        : 2.0f;
    glm::vec3 velocity =
        glm::normalize(moveDir) * sprint * moveSpeed * context.frameInfo.dt;

    // Helper lambda to check if a specific position collides with any entity
    auto checkCollision = [&](const glm::vec3 &testPos) {
      // Shrunk the player slightly so they don't scrape the walls in 1x1
      // corridors
      const AABB playerAABB{testPos + glm::vec3{-0.2f, -0.4f, -0.2f},
                            testPos + glm::vec3{0.2f, 0.4f, 0.2f}};

      for (auto &entity : entities) {
        // Get the raw AABB from the vertex data
        glm::vec3 min = entity.getMesh().aabb.min;
        glm::vec3 max = entity.getMesh().aabb.max;

        // Define the 8 local corners of the raw AABB
        glm::vec3 corners[8] = {{min.x, min.y, min.z}, {min.x, min.y, max.z},
                                {min.x, max.y, min.z}, {min.x, max.y, max.z},
                                {max.x, min.y, min.z}, {max.x, min.y, max.z},
                                {max.x, max.y, min.z}, {max.x, max.y, max.z}};

        AABB worldAABB; // Initializes to proper min/max limits
        for (int i = 0; i < 8; ++i) {
          // 1. Apply the glTF Node Transform (fixes models that are
          // rotated/offset in Blender)
          glm::vec3 localPos = glm::vec3(entity.getMesh().transform *
                                         glm::vec4(corners[i], 1.0f));

          // 2. Apply Entity Scale
          localPos *= entity.transform.scale;

          // 3. Apply Entity Rotation & Position
          glm::vec3 worldPos = entity.transform.orientation * localPos +
                               entity.transform.position;

          // Re-find the absolute minimums and maximums for the new world
          // orientation
          worldAABB.min = glm::min(worldAABB.min, worldPos);
          worldAABB.max = glm::max(worldAABB.max, worldPos);
        }

        if (worldAABB.intersects(playerAABB)) {
          return true; // Collision detected!
        }
      }
      return false;
    };

    // Apply sliding collision by testing each axis independently
    glm::vec3 newPosition = context.camera.position;

    if (!checkCollision(newPosition + glm::vec3{velocity.x, 0.f, 0.f})) {
      newPosition.x += velocity.x;
    }
    if (!checkCollision(newPosition + glm::vec3{0.f, velocity.y, 0.f})) {
      newPosition.y += velocity.y;
    }
    if (!checkCollision(newPosition + glm::vec3{0.f, 0.f, velocity.z})) {
      newPosition.z += velocity.z;
    }

    context.camera.position = newPosition;
  }
}
} // namespace input
} // namespace vkh
