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

        fastgltf = pkgs.stdenv.mkDerivation {
          pname = "fastgltf";
          version = "0.9.0";

          src = pkgs.fetchFromGitHub {
            owner = "spnda";
            repo = "fastgltf";
            rev = "c8bbb236822a55d320568e8b0a0042fc1ace6a8e";
            sha256 = "sha256-Pi2HS5iAGPvfFV20HXMct+Dyyghyzm7Q35FJq3+L2Xw==";
          };

          nativeBuildInputs = with pkgs; [
            cmake
          ];
          buildInputs = with pkgs; [
            simdjson
          ];
          cmakeFlags = [
            "-DFASTGLTF_USE_SYSTEM_SIMDJSON=ON"
          ];
        };
      in
      {
        devShells.default = pkgs.mkShell {
          name = "devenv";

          packages = with pkgs; [
            vulkan-loader
            vulkan-validation-layers
            vulkan-tools
            vulkan-headers
            glfw
            glm
            cmake
            glslang
            openal
            stb
            curl
            ktx-tools
            magic-enum
            simdjson
            fastgltf
            gcc
            enet
            pkg-config
            spirv-headers
            spirv-tools
            shaderc
            opusfile
            libogg
          ];

          shellHook = ''
            exec $(getent passwd "$USER" | cut -d: -f7)
            export CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH:${fastgltf}:${pkgs.simdjson}
          '';
        };

        defaultPackage = pkgs.stdenv.mkDerivation {
          pname = "vulkhan";
          version = "0.0.0";
          src = ./.;

          nativeBuildInputs = with pkgs; [
            cmake
            makeWrapper
            copyDesktopItems
          ];

          buildInputs = with pkgs; [
            vulkan-loader
            vulkan-headers
            glfw
            glm
            openal
            curl
            ktx-tools
            magic-enum
            stb
            glslang
            simdjson
            fastgltf
            spirv-headers
            spirv-tools
            enet
            pkg-config
            shaderc
            opusfile
            libogg
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
