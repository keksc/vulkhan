cmake_minimum_required(VERSION 3.10)

if(WIN32)
  include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

project("vulkhan")

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(fmt REQUIRED)
find_package(OpenAL REQUIRED)

add_subdirectory(3rdparty/fastgltf)

link_libraries(Vulkan::Vulkan glfw fmt::fmt OpenAL::OpenAL fastgltf::fastgltf)
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(FLAGS WIN32)
else()
  set(FLAGS)
endif()

add_executable(vulkhan ${FLAGS} ${SOURCES})

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/shaders")
find_program(
  GLSL_VALIDATOR glslangValidator
  HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
        /usr/bin
        /usr/local/bin
        ${VULKAN_SDK_PATH}/Bin
        ${VULKAN_SDK_PATH}/Bin32
        $ENV{VULKAN_SDK}/Bin/
        $ENV{VULKAN_SDK}/Bin32/)

file(GLOB_RECURSE GLSL_SOURCE_FILES "${PROJECT_SOURCE_DIR}/shaders/*.frag"
     "${PROJECT_SOURCE_DIR}/shaders/*.vert"
     "${PROJECT_SOURCE_DIR}/shaders/*.comp")

foreach(GLSL ${GLSL_SOURCE_FILES})
  get_filename_component(FILE_NAME ${GLSL} NAME)
  set(SPIRV "${PROJECT_BINARY_DIR}/shaders/${FILE_NAME}.spv")
  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSL_VALIDATOR} -V ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL})
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach(GLSL)

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})

add_dependencies(vulkhan Shaders)

file(COPY "${PROJECT_SOURCE_DIR}/sounds" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/models" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/textures" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/fonts" DESTINATION "${PROJECT_BINARY_DIR}")

install(TARGETS vulkhan DESTINATION bin)
install(IMPORTED_RUNTIME_ARTIFACTS fmt::fmt glfw DESTINATION bin)
install(FILES ${PROJECT_BINARY_DIR}/OpenAL32.dll DESTINATION bin)
install(DIRECTORY ${PROJECT_BINARY_DIR}/shaders ${PROJECT_BINARY_DIR}/sounds
                  ${PROJECT_BINARY_DIR}/models DESTINATION bin)
