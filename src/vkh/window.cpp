#include "window.hpp"

#include "engineContext.hpp"

#include <GLFW/glfw3.h>
#ifdef _WIN32
#include <stb_image.h>
#else
#include <stb/stb_image.h>
#endif

#include <filesystem>
#include <stdexcept>

namespace vkh {

vk::Extent2D Window::getExtent() {
  return {static_cast<uint32_t>(size.x), static_cast<uint32_t>(size.y)};
}

void Window::setFullscreen(bool fullscreen) {
  if (!fullscreen) {
    glfwSetWindowMonitor(*this, nullptr, 100, 100, 800, 600, 0);
  } else {
    GLFWmonitor *monitor = glfwGetPrimaryMonitor();
    const GLFWvidmode *mode = monitor ? glfwGetVideoMode(monitor) : nullptr;
    if (!mode)
      return;
    glfwSetWindowMonitor(*this, monitor, 0, 0, mode->width, mode->height,
                         mode->refreshRate);
  }
}

bool Window::isFocused() { return glfwGetWindowAttrib(*this, GLFW_FOCUSED); }
Window::operator GLFWwindow *() { return glfwWindow; }

void framebufferResizeCallback(GLFWwindow *window, int width, int height) {
  auto context =
      reinterpret_cast<EngineContext *>(glfwGetWindowUserPointer(window));
  context->window.framebufferResized = true;
  context->window.size.x = width;
  context->window.size.y = height;
  if (width > 0 && height > 0)
    context->window.aspectRatio = static_cast<float>(width) / height;
}

GLFWcursor *loadCursor(const std::filesystem::path &path) {
  int w, h, texChannels;
  stbi_uc *pixels =
      stbi_load(path.string().c_str(), &w, &h, &texChannels, STBI_rgb_alpha);
  if (!pixels)
    return glfwCreateStandardCursor(GLFW_ARROW_CURSOR);

  GLFWimage image;
  image.width = w;
  image.height = h;
  image.pixels = pixels;

  GLFWcursor *cursor = glfwCreateCursor(&image, 0, 0);
  stbi_image_free(pixels);
  return cursor ? cursor : glfwCreateStandardCursor(GLFW_ARROW_CURSOR);
}

void initWindow(EngineContext &context) {
  glfwInitHint(GLFW_WAYLAND_LIBDECOR, GLFW_FALSE);
  if (!glfwInit())
    throw std::runtime_error("Failed to initialize GLFW");
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
  glfwWindowHint(GLFW_FLOATING, GLFW_TRUE);
  glfwWindowHint(GLFW_DECORATED, GLFW_FALSE);
  context.window.glfwWindow =
      glfwCreateWindow(context.window.size.x, context.window.size.y, "Vulkhan",
                       nullptr, nullptr);
  if (!context.window.glfwWindow) {
    glfwTerminate();
    throw std::runtime_error("Failed to create GLFW window");
  }
  glfwSetWindowUserPointer(context.window, &context);
  glfwSetFramebufferSizeCallback(context.window, framebufferResizeCallback);
  glfwGetFramebufferSize(context.window, &context.window.size.x,
                         &context.window.size.y);
  if (context.window.size.x > 0 && context.window.size.y > 0)
    context.window.aspectRatio = static_cast<float>(context.window.size.x) /
                                 static_cast<float>(context.window.size.y);
  context.window.cursors.arrow = loadCursor("textures/arrow.png");
  glfwSetCursor(context.window, context.window.cursors.arrow);
  context.window.cursors.ibeam = glfwCreateStandardCursor(GLFW_IBEAM_CURSOR);
  glfwGetWindowContentScale(context.window, &context.window.contentScale.x,
                            &context.window.contentScale.y);
}

void cleanupWindow(EngineContext &context) {
  glfwDestroyCursor(context.window.cursors.arrow);
  glfwDestroyCursor(context.window.cursors.ibeam);
  glfwDestroyWindow(context.window);
  glfwTerminate();
}

} // namespace vkh
