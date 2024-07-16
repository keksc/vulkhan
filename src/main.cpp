#include <fmt/core.h>
#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "details.hpp"
#include "pipeline.hpp"

void run() {
    details details{};
    glfwInit();
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_FALSE);
    
    const int w = 800, h = 600;

    details.window = glfwCreateWindow(w, h, "Nameless", nullptr, nullptr);
    details.device = std::make_unique<Device>(details.window);
    Pipeline pipeline(*details.device, "../shaders/shader.vert.spv", "../shaders/shader.frag.spv", Pipeline::defaultPipelineConfigInfo(w, h));
    while (!glfwWindowShouldClose(details.window)) {
        glfwPollEvents();
    }
    glfwDestroyWindow(details.window);
    glfwTerminate();
}

int main() {
    try {
        run();
    } catch(const std::exception &e) {
        fmt::print("{}\n", e.what());
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}