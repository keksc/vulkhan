{
  description = "devenv";

  inputs = {
    nixpkgs.url = "github:nixos/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs =
    {
      self,
      nixpkgs,
      flake-utils,
    }:
    flake-utils.lib.eachDefaultSystem (
      system:
      let
        pkgs = nixpkgs.legacyPackages.${system};
        llvmPkgs = pkgs.llvmPackages;

        fastgltf = pkgs.stdenv.mkDerivation {
          pname = "fastgltf";
          version = "0.9.0";
          src = pkgs.fetchFromGitHub {
            owner = "spnda";
            repo = "fastgltf";
            rev = "c8bbb236822a55d320568e8b0a0042fc1ace6a8e";
            sha256 = "sha256-Pi2HS5iAGPvfFV20HXMct+Dyyghyzm7Q35FJq3+L2Xw==";
          };
          nativeBuildInputs = [ pkgs.cmake ];
          buildInputs = [ pkgs.simdjson ];
          cmakeFlags = [ "-DFASTGLTF_USE_SYSTEM_SIMDJSON=ON" ];
        };
      in
      {
        devShells.default = pkgs.mkShell {
          name = "devenv";

          packages = [
            pkgs.vulkan-loader
            pkgs.vulkan-validation-layers
            pkgs.vulkan-tools
            pkgs.vulkan-headers
            pkgs.glfw
            pkgs.glm
            pkgs.cmake
            pkgs.glslang
            pkgs.ninja
            pkgs.openal
            pkgs.stb
            pkgs.curl
            pkgs.ktx-tools
            pkgs.magic-enum
            pkgs.simdjson
            fastgltf
            pkgs.gcc
            pkgs.enet
            pkgs.pkg-config
            pkgs.spirv-headers
            pkgs.spirv-tools
            pkgs.shaderc
            pkgs.opusfile
            pkgs.libogg
            pkgs.wasmtime
            
            # WASM modding
            llvmPkgs.lld
            pkgs.binaryen
            pkgs.wabt
            pkgs.wamr
          ];

          shellHook = ''
            # Exposing explicit paths for the WASM toolchain to read without overriding host CC/CXX
            export WASM_CLANG="${llvmPkgs.clang-unwrapped}/bin/clang"
            export WASM_CLANGXX="${llvmPkgs.clang-unwrapped}/bin/clang++"
            
            export CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH:${fastgltf}:${pkgs.simdjson}
            exec $(getent passwd "$USER" | cut -d: -f7)
          '';
        };

        defaultPackage = pkgs.stdenv.mkDerivation {
          pname = "vulkhan";
          version = "0.0.0";
          src = ./.;

          nativeBuildInputs = [
            pkgs.cmake
            pkgs.makeWrapper
            pkgs.copyDesktopItems
          ];

          buildInputs = [
            pkgs.vulkan-loader
            pkgs.vulkan-headers
            pkgs.glfw
            pkgs.glm
            pkgs.openal
            pkgs.curl
            pkgs.ktx-tools
            pkgs.magic-enum
            pkgs.stb
            pkgs.glslang
            pkgs.simdjson
            fastgltf
            pkgs.spirv-headers
            pkgs.spirv-tools
            pkgs.enet
            pkgs.pkg-config
            pkgs.ninja
            pkgs.shaderc
            pkgs.opusfile
            pkgs.libogg
            pkgs.wasmtime

            llvmPkgs.lld
          ];

          desktopItems = [
            (pkgs.makeDesktopItem {
              name = "vulkhan";
              exec = "vulkhan";
              icon = "vulkhan";
              comment = "lovely vulkan graphics engine";
              desktopName = "Vulkhan";
              categories = [ "Game" "Development" ];
            })
          ];

          postInstall = ''
            install -Dm644 $src/icon.png $out/share/icons/hicolor/256x256/apps/vulkhan.png

            wrapProgram $out/bin/vulkhan \
              --prefix LD_LIBRARY_PATH : "${pkgs.lib.makeLibraryPath [ pkgs.vulkan-loader ]}"
          '';
        };
      }
    );
}
