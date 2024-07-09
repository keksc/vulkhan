#include <GLFW/glfw3.h>
#include <fmt/core.h>

#include "init.hpp"

void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods);
void framebufferSizeCallback(GLFWwindow* window, int width, int height);
int main()
{
	glfwInit();
	glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
	GLFWwindow* window = glfwCreateWindow(600, 400, "vulkhan", nullptr, nullptr);
	glfwSetKeyCallback(window, keyCallback);
	glfwSetFramebufferSizeCallback(window, framebufferSizeCallback);
	VkDetails details{};
	try {
		init(details);
	}
	catch (const std::exception& e) {
		fmt::print("{}\n", e.what());
	}
	while (!glfwWindowShouldClose(window)) {
		glfwPollEvents();
	}
	glfwDestroyWindow(window);
	glfwTerminate();
}
void keyCallback(GLFWwindow* window, int key, int scancode, int action, int mods) {
	if (action == GLFW_PRESS) fmt::print("press");
	if (action == GLFW_RELEASE) fmt::print("release");
}
void framebufferSizeCallback(GLFWwindow* window, int width, int height) {

}