#include "window.hpp"

namespace vkh {
void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->window.framebufferResized = true;
  context->window.width = width;
  context->window.height = height;
  context->window.aspectRatio = static_cast<float>(width) / height;
}
void initWindow(EngineContext &context) {
  glfwInit();
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
  context.window.glfwWindow = glfwCreateWindow(
      context.window.width, context.window.height, "Vulkhan", nullptr, nullptr);
  glfwSetWindowUserPointer(context.window, &context);
  glfwSetFramebufferSizeCallback(context.window, framebufferResizeCallback);
}
void cleanupWindow(EngineContext &context) {
  glfwDestroyWindow(context.window);
  glfwTerminate();
}
} // namespace vkh