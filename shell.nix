{
  pkgs ? import <nixpkgs> {},
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
    glslang
    openal
    clang-tools
    cmake-language-server
    stb
    freetype
  ];

  # Environment variables for Vulkan
  shellHook = ''
    export VK_LAYER_PATH=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d
    export VK_DRIVER_FILES=/run/opengl-driver/share/vulkan/icd.d/nvidia_icd.x86_64.json
  '';
}
