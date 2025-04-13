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
find_package(CURL REQUIRED)

add_subdirectory(3rdparty/fastgltf)

link_directories(3rdparty/lib)
include_directories(3rdparty/include)
link_libraries(
  Vulkan::Vulkan
  glfw
  fmt::fmt
  OpenAL::OpenAL
  fastgltf::fastgltf
  GameAnalytics
  CURL::libcurl)
file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(FLAGS WIN32)
else()
  set(FLAGS)
endif()

add_executable(vulkhan ${FLAGS} ${SOURCES})

file(MAKE_DIRECTORY "${PROJECT_BINARY_DIR}/shaders")

find_program(
  GLSLC glslc
  HINTS ${Vulkan_GLSLANG_VALIDATOR_EXECUTABLE}
        /usr/bin
        /usr/local/bin
        ${VULKAN_SDK_PATH}/Bin
        ${VULKAN_SDK_PATH}/Bin32
        $ENV{VULKAN_SDK}/Bin/
        $ENV{VULKAN_SDK}/Bin32/)

# Recursively find all shaders in subdirectories
file(GLOB_RECURSE GLSL_SOURCE_FILES 
  "${PROJECT_SOURCE_DIR}/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/shaders/*.vert"
  "${PROJECT_SOURCE_DIR}/shaders/*.comp"
)

foreach(GLSL ${GLSL_SOURCE_FILES})
  # Get relative path from PROJECT_SOURCE_DIR/shaders
  file(RELATIVE_PATH RELATIVE_GLSL_PATH 
    "${PROJECT_SOURCE_DIR}/shaders" 
    ${GLSL}
  )
  
  # Split into directory and filename components
  get_filename_component(RELATIVE_DIR ${RELATIVE_GLSL_PATH} DIRECTORY)
  get_filename_component(FILE_NAME ${RELATIVE_GLSL_PATH} NAME)

  # Create output directory
  set(SPIRV_OUTPUT_DIR "${PROJECT_BINARY_DIR}/shaders/${RELATIVE_DIR}")
  file(MAKE_DIRECTORY ${SPIRV_OUTPUT_DIR})

  # Set output path
  set(SPIRV "${SPIRV_OUTPUT_DIR}/${FILE_NAME}.spv")

  add_custom_command(
    OUTPUT ${SPIRV}
    COMMAND ${GLSLC} ${GLSL} -o ${SPIRV}
    DEPENDS ${GLSL}
    COMMENT "Compiling shader ${RELATIVE_GLSL_PATH}"
  )
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(vulkhan Shaders)

file(COPY "${PROJECT_SOURCE_DIR}/models" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/fonts" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/sounds" DESTINATION "${PROJECT_BINARY_DIR}")

install(TARGETS vulkhan DESTINATION bin)
install(IMPORTED_RUNTIME_ARTIFACTS fmt::fmt glfw DESTINATION bin)
install(FILES ${PROJECT_BINARY_DIR}/OpenAL32.dll DESTINATION bin)
install(DIRECTORY ${PROJECT_BINARY_DIR}/shaders ${PROJECT_BINARY_DIR}/sounds
                  ${PROJECT_BINARY_DIR}/models DESTINATION bin)
