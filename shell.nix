{
  pkgs ? import <nixpkgs> { },
}:

pkgs.mkShell {
  name = "devenv";

  buildInputs = with pkgs; [
    vulkan-loader
    vulkan-validation-layers
    vulkan-tools
    vulkan-headers
    glfw
    fmt
    tinyobjloader
    glm
    cmake
    shaderc
    openal
    clang-tools
    cmake-language-server
    stb
    simdjson
  ];

  shellHook = ''
    export VK_LAYER_PATH=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d
    export VK_DRIVER_FILES=/run/opengl-driver/share/vulkan/icd.d/nvidia_icd.x86_64.json
    export FFTW3_DIR=${pkgs.fftw.dev}/lib/cmake/fftw3
  '';
}
