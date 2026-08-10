cmake_minimum_required(VERSION 3.28)

if(NOT DEFINED CMAKE_TOOLCHAIN_FILE)
  set(CMAKE_TOOLCHAIN_FILE
      "${CMAKE_CURRENT_SOURCE_DIR}/wasm64-toolchain.cmake"
      CACHE FILEPATH "Wasm64 Toolchain")
endif()

project(WasmModding CXX)

set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

include(CheckIPOSupported)
check_ipo_supported(RESULT lto_supported OUTPUT error)
if(lto_supported)
  set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
endif()

if(NOT DEFINED MODS_OUTPUT_DIR)
  set(MODS_OUTPUT_DIR "${CMAKE_CURRENT_BINARY_DIR}/mods")
endif()
file(MAKE_DIRECTORY "${MODS_OUTPUT_DIR}")

# One mod per immediate subdirectory of modding/. A subdirectory is a mod if
# it contains at least one .cpp file. The mod's target name is the folder
# name (e.g. modding/betterNothing/*.cpp -> betterNothing.oneirosMod), and
# all .cpp files inside that folder (recursively) are compiled into it.
file(GLOB MOD_DIR_CANDIDATES RELATIVE "${CMAKE_CURRENT_SOURCE_DIR}"
     "${CMAKE_CURRENT_SOURCE_DIR}/*")

set(MOD_BINARIES "")
set(MOD_NAMES "")

foreach(ENTRY ${MOD_DIR_CANDIDATES})
  set(MOD_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${ENTRY}")
  if(IS_DIRECTORY "${MOD_DIR}")
    file(GLOB_RECURSE MOD_SOURCES "${MOD_DIR}/*.cpp")
    if(MOD_SOURCES)
      set(MOD_NAME "${ENTRY}.oneirosMod")

      add_executable(${MOD_NAME} ${MOD_SOURCES})
      set_target_properties(
        ${MOD_NAME} PROPERTIES
        RUNTIME_OUTPUT_DIRECTORY "${MODS_OUTPUT_DIR}"
        OUTPUT_NAME "${ENTRY}"
        SUFFIX ".oneirosMod")

      list(APPEND MOD_BINARIES "${MODS_OUTPUT_DIR}/${ENTRY}.oneirosMod")
      list(APPEND MOD_NAMES "${MOD_NAME}")
    endif()
  endif()
endforeach()

if(NOT MOD_BINARIES)
  message(WARNING "No mod subdirectories with .cpp sources found under ${CMAKE_CURRENT_SOURCE_DIR}")
endif()

# Recorded in CMakeCache.txt so the parent (top-level) CMakeLists.txt can
# read back the exact list of expected output files and target names without
# needing to duplicate this discovery logic.
set(MOD_BINARIES "${MOD_BINARIES}" CACHE INTERNAL "Full paths of all built mod binaries")
set(MOD_NAMES "${MOD_NAMES}" CACHE INTERNAL "CMake target names of all mods, parallel to MOD_BINARIES")
