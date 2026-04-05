#pragma once

#include <GLFW/glfw3.h>
#include <vulkan/vulkan_core.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>

namespace vkh {
class EngineContext;
struct Window {
  glm::ivec2 size = {800, 600};
  bool framebufferResized = false;
  VkExtent2D getExtent();
  void setFullscreen(bool fullscreen);
  bool isFocused();

  float aspectRatio = static_cast<float>(size.x) / static_cast<float>(size.y);
  operator GLFWwindow *();
  GLFWwindow *glfwWindow;
  struct {
    GLFWcursor *arrow;
    GLFWcursor *ibeam;
  } cursors;
};

void initWindow(EngineContext &context);
void cleanupWindow(EngineContext &context);
} // namespace vkh
