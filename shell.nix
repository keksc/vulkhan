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
    glm
    cmake
    glslang
    openal
    clang-tools
    # cmake-language-server
    stb
    simdjson
    curl
    gcc
    ktx-tools
    magic-enum
  ];

  shellHook = ''
    export VK_LAYER_PATH=${pkgs.vulkan-validation-layers}/share/vulkan/explicit_layer.d
  '';
}
