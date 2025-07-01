#include "window.hpp"
#include <GLFW/glfw3.h>

namespace vkh {
void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->window.framebufferResized = true;
  context->window.size.x = width;
  context->window.size.y = height;
  context->window.aspectRatio = static_cast<float>(width) / height;
}
void initWindow(EngineContext &context) {
  glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_FALSE);
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  context.window.glfwWindow =
      glfwCreateWindow(context.window.size.x, context.window.size.y, "Vulkhan",
                       nullptr, nullptr);
  glfwSetWindowUserPointer(context.window, &context);
  glfwSetFramebufferSizeCallback(context.window, framebufferResizeCallback);
  glfwGetWindowSize(context.window, &context.window.size.x,
                    &context.window.size.y);
  context.window.aspectRatio = static_cast<float>(context.window.size.x) /
                 static_cast<float>(context.window.size.y);
}
void cleanupWindow(EngineContext &context) {
  glfwDestroyWindow(context.window);
  glfwTerminate();
}
} // namespace vkh
