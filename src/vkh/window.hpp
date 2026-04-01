#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
#define GLM_FORCE_RADIANS
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <AL/al.h>
#include <AL/alc.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vkh {
class EngineContext;
struct Window {
  glm::ivec2 size = {800, 600};
  bool framebufferResized = false;
  VkExtent2D getExtent() {
    return {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)};
  };
  void setFullscreen(bool fullscreen) {
    if (fullscreen) {
      glfwSetWindowMonitor(*this, NULL, 100, 100, 800, 600, 0);
    } else {
      GLFWmonitor *monitor = glfwGetPrimaryMonitor();
      const GLFWvidmode *mode = glfwGetVideoMode(monitor);
      glfwSetWindowMonitor(*this, monitor, 0, 0, mode->width, mode->height,
                           mode->refreshRate);
    }
  }
  bool isFocused() { return glfwGetWindowAttrib(*this, GLFW_FOCUSED); }

  float aspectRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
  operator GLFWwindow *() { return glfwWindow; }
  GLFWwindow *glfwWindow;
  struct {
    GLFWcursor *arrow;
    GLFWcursor *ibeam;
  } cursors;
};

void initWindow(EngineContext &context);
void cleanupWindow(EngineContext &context);
} // namespace vkh
