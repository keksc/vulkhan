# Repository Guidelines

## Project Structure & Module Organization

Vulkhan is a C++23 Vulkan graphics engine/application. `src/main.cpp` is the entry point; `src/vkh/` contains Vulkan infrastructure and rendering systems. Gameplay, UI, items, and networking live in `src/entities/`, `src/UI/`, `src/items/`, and `src/network/`.

`shaders/` contains GLSL stages and shared includes grouped by rendering system. Runtime assets live in `models/`, `textures/`, `fonts/`, and `sounds/`. `modding/<modName>/` holds C++ sources compiled to wasm64 `.oneirosMod` files. `3rdparty/` contains vendored dependencies; `cmake/` contains discovery helpers. No automated test directory is currently present.

## Build, Test, and Development Commands

Run these commands from the repository root:

- `nix develop`: enter the environment defined in `flake.nix`.
- `cmake -S . -B build -DCMAKE_BUILD_TYPE=Debug`: configure the application and mods; requires CMake 3.28 or newer.
- `cmake --build build -j`: compile C++, GLSL to SPIR-V, and WASM mods.
- `cmake --build build --target shaders`: rebuild shader stages only.
- `cd build` followed by `./vulkhan`: launch with copied assets and compiled shaders.

Outside Nix, install the dependencies listed in `CMakeLists.txt` and provide `glslc`, `WASM_CLANG`, and `WASM_CLANGXX`; the WASM toolchain also needs LLVM's linker. Reconfigure after adding C++ files, shader stages, mods, or changing assets copied during configuration.

## Coding Style & Naming Conventions

Use `.cpp` implementations and `.hpp` headers. Follow `.clang-format`: two-space indentation, spaces instead of tabs, attached braces, and an 80-column limit. Format changed C++ files with `clang-format -i path/to/file.cpp`.

Match neighboring code: types generally use PascalCase; functions, variables, and filenames commonly use camelCase, such as `updateBoids` and `dungeonGenerator.hpp`. Keep shader and CPU-side interfaces consistent. No dedicated lint workflow is configured.

## Testing Guidelines

No automated testing framework or coverage threshold is configured. Build in Debug and manually exercise affected rendering, UI, networking, or mod behavior. Install `VK_LAYER_KHRONOS_validation` for Debug runs and inspect validation output. Document reproduction steps and results; include visual evidence for rendering changes.

## Commit & Pull Request Guidelines

History uses short, informal descriptive subjects without a consistent Conventional Commits scheme. Write concise, action-oriented subjects, such as `Fix Vulkan validation errors in entity rendering`.

Keep changes focused. Pull requests should explain the behavior change, link relevant issues, and list build and manual verification results. Include screenshots or recordings for visual changes and preserve asset attribution and licenses.
