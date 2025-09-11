#include "window.hpp"

#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <stb_image.h>
#else
#include <stb/stb_image.h>
#endif

#include <filesystem>

namespace vkh {
void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->window.framebufferResized = true;
  context->window.size.x = width;
  context->window.size.y = height;
  context->window.aspectRatio = static_cast<float>(width) / height;
}
void loadCursor(EngineContext &context, const std::filesystem::path &path,
                GLFWcursor *cursor) {
  int w, h, texChannels;
  stbi_uc *pixels =
      stbi_load(path.string().c_str(), &w, &h, &texChannels, STBI_rgb_alpha);

  GLFWimage image;
  image.width = w;
  image.height = h;
  image.pixels = pixels;

  context.window.cursors.arrow = glfwCreateCursor(&image, 0, 0);
  stbi_image_free(pixels);
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
  loadCursor(context, "textures/arrow.png", context.window.cursors.arrow);
  glfwSetCursor(context.window, context.window.cursors.arrow);
  context.window.cursors.ibeam = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
}
void cleanupWindow(EngineContext &context) {
  glfwDestroyCursor(context.window.cursors.arrow);
  glfwDestroyCursor(context.window.cursors.ibeam);
  glfwDestroyWindow(context.window);
  glfwTerminate();
}
} // namespace vkh
