#pragma once

#define GLFW_INCLUDE_VULKAN
#include <GLFW/glfw3.h>

#include "device.hpp"

struct details {
    GLFWwindow* window;
    std::shared_ptr<Device> device;
};