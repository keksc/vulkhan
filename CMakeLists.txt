cmake_minimum_required(VERSION 3.10)

if(WIN32)
  include("$ENV{VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake")
endif()
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(GLFW_INCLUDE_VULKAN)
set(FASTGLTF_COMPILE_AS_CPP20)
set(FASTGLTF_ENABLE_CPP_MODULES)
set(FASTGLFW_USE_STD_MODULE)

project("vulkhan")

set(CMAKE_MODULE_PATH ${CMAKE_MODULE_PATH} "${PROJECT_SOURCE_DIR}/cmake")

find_package(Vulkan REQUIRED)
find_package(glfw3 3.3 REQUIRED)
find_package(OpenAL REQUIRED)
# find_package(CURL REQUIRED)
find_package(simdjson REQUIRED)
find_package(fastgltf REQUIRED)
find_package(glslang REQUIRED)
find_package(PkgConfig REQUIRED)
pkg_check_modules(ENET REQUIRED libenet)

add_library(VkFFT INTERFACE)
target_compile_definitions(VkFFT INTERFACE -DVKFFT_BACKEND=0
                                           -DVKFFT_MAX_FFT_DIMENSIONS=2)
link_directories(3rdparty/lib)
include_directories(3rdparty/include PUBLIC
                    ${CMAKE_CURRENT_SOURCE_DIR}/3rdparty/vkFFT/)
link_libraries(
  Vulkan::Vulkan
  glfw
  OpenAL::OpenAL
  fastgltf::fastgltf
  # CURL::libcurl
  glslang
  VkFFT
  ktx
  enet)

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/src/*.cpp)
if(CMAKE_BUILD_TYPE STREQUAL "Release")
  set(FLAGS WIN32)
else()
  set(FLAGS)
endif()

add_executable(vulkhan ${FLAGS} ${SOURCES})
target_precompile_headers(vulkhan INTERFACE src/vkh/engineContext.hpp)

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

set(GLSL_DEBUG_FLAGS "")
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
  # -gVS = glslang source-level debug info (RenderDoc-compatible), for
  # glsLangValidator
  set(GLSL_DEBUG_FLAGS "-g")
endif()
file(
  GLOB_RECURSE
  GLSL_SOURCE_FILES
  "${PROJECT_SOURCE_DIR}/shaders/*.frag"
  "${PROJECT_SOURCE_DIR}/shaders/*.vert"
  "${PROJECT_SOURCE_DIR}/shaders/*.comp"
  "${PROJECT_SOURCE_DIR}/shaders/*.tese"
  "${PROJECT_SOURCE_DIR}/shaders/*.tesc")

foreach(GLSL ${GLSL_SOURCE_FILES})
  # Get relative path from PROJECT_SOURCE_DIR/shaders
  file(RELATIVE_PATH RELATIVE_GLSL_PATH "${PROJECT_SOURCE_DIR}/shaders" ${GLSL})

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
    COMMAND ${GLSLC} ${GLSL} -o ${SPIRV} --target-env=vulkan1.1
            -DUSE_SUBGROUP_OPS ${GLSL_DEBUG_FLAGS}
    DEPENDS ${GLSL}
    COMMENT "Compiling shader ${RELATIVE_GLSL_PATH}")
  list(APPEND SPIRV_BINARY_FILES ${SPIRV})
endforeach()

add_custom_target(Shaders DEPENDS ${SPIRV_BINARY_FILES})
add_dependencies(vulkhan Shaders)

file(COPY "${PROJECT_SOURCE_DIR}/models" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/fonts" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/sounds" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/textures" DESTINATION "${PROJECT_BINARY_DIR}")
file(COPY "${PROJECT_SOURCE_DIR}/flamegraph.sh"
     DESTINATION "${PROJECT_BINARY_DIR}")

install(TARGETS vulkhan DESTINATION bin)
install(IMPORTED_RUNTIME_ARTIFACTS glfw DESTINATION bin)
if(WIN32)
  install(FILES ${PROJECT_BINARY_DIR}/OpenAL32.dll DESTINATION bin)
endif()
install(DIRECTORY ${PROJECT_BINARY_DIR}/shaders ${PROJECT_BINARY_DIR}/fonts
                  ${PROJECT_BINARY_DIR}/sounds ${PROJECT_BINARY_DIR}/models
                  ${PROJECT_BINARY_DIR}/textures DESTINATION bin)

file(GLOB_RECURSE SOURCES ${PROJECT_SOURCE_DIR}/server/*.cpp)
add_executable(vulkhanServer ${SOURCES})
