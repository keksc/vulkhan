{
  description = "C++ to WASM Modding Environment (Nix Cross-Compiler)";

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
      in
      {
        devShells.default = pkgs.mkShell {
          name = "wasm-cross-devenv";

          packages = [
            pkgs.cmake
            llvmPkgs.lld
            pkgs.binaryen
            pkgs.wabt
            pkgs.wamr
            pkgs.wamrc
          ];

          shellHook = ''
            export CC="${llvmPkgs.clang-unwrapped}/bin/clang"
            export CXX="${llvmPkgs.clang-unwrapped}/bin/clang++"
            
            exec $(getent passwd "$USER" | cut -d: -f7)
          '';
        };
      }
    );
}
