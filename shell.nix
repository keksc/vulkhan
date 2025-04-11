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
    glm
    cmake
    glslang
    openal
    clang-tools
    cmake-language-server
    stb
    simdjson
    curl
    gcc
  ];

  shellHook = ''
    export VK_LAYER_PATH=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d
  '';
}
