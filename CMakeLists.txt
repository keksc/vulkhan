cmake_minimum_required (VERSION 3.8)

include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
set(CMAKE_CXX_STANDARD 20)

project ("vulkhan")

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(fmt REQUIRED)


link_libraries(Vulkan::Vulkan glfw fmt::fmt)
add_executable (vulkhan "src/main.cpp" "src/pipeline.cpp" "src/device.cpp")