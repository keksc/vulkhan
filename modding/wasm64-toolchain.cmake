set(CMAKE_SYSTEM_NAME Generic)
set(CMAKE_SYSTEM_PROCESSOR wasm64)

# Force CMake to use the exact unwrapped Clang binaries we allocated for Wasm
if(DEFINED ENV{WASM_CLANG})
  set(CMAKE_C_COMPILER $ENV{WASM_CLANG})
else()
  set(CMAKE_C_COMPILER clang)
endif()

if(DEFINED ENV{WASM_CLANGXX})
  set(CMAKE_CXX_COMPILER $ENV{WASM_CLANGXX})
else()
  set(CMAKE_CXX_COMPILER clang++)
endif()

set(WASM_FLAGS "-target wasm64-unknown-none -nostdlib")

set(CMAKE_C_FLAGS
    "${WASM_FLAGS}"
    CACHE STRING "" FORCE)
set(CMAKE_CXX_FLAGS
    "${WASM_FLAGS}"
    CACHE STRING "" FORCE)

set(CMAKE_EXE_LINKER_FLAGS
    "-fuse-ld=lld -Wl,--no-entry -Wl,--gc-sections -Wl,--strip-all"
    CACHE STRING "" FORCE)

set(CMAKE_EXECUTABLE_SUFFIX ".wasm")
