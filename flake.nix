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
            simdjson
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
            shaderc
            openal
            stb
            simdjson
            curl
            ktx-tools
            magic-enum
            gcc
            fastgltf
            simdjson
            stdenv.cc
          ];

          shellHook = ''
            exec $(getent passwd "$USER" | cut -d: -f7)
            export CMAKE_PREFIX_PATH=$CMAKE_PREFIX_PATH:${fastgltf}:${pkgs.simdjson}
          '';
        };
        defaultPackage = pkgs.stdenv.mkDerivation {
          name = "vulkhan";
          src = ./.;
          configurePhase = "cmake -B build -DCMAKE_BUILD_TYPE=Release";
          buildPhase = "cmake --build build";
          installPhase = "cmake --install build --prefix $out";

          nativeBuildInputs = with pkgs; [
            cmake
            shaderc
          ];

          buildInputs = with pkgs; [
            vulkan-loader
            vulkan-headers
            glfw
            glm
            openal
            curl
            simdjson
            ktx-tools
            magic-enum
            stb
            glslang
          ];
        };
      }
    );
}
