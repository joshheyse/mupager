{
  description = "mupdf.nvim - Terminal document viewer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = import nixpkgs {inherit system;};
      in {
        devShells.default = pkgs.mkShell {
          name = "mupdf-nvim";

          packages = with pkgs; [
            # Compilers
            gcc14
            clang_19
            llvmPackages_19.bintools

            # Build tools
            cmake
            ninja
            pkg-config

            # Debugging / profiling
            gdb
            valgrind

            # C++ formatting / linting
            clang-tools

            # Lua formatting / linting
            stylua
            selene

            # Task runner
            just

            # Libraries
            mupdf
            msgpack-cxx
          ];

          shellHook = ''
            export CC=clang
            export CXX=clang++
            unset NIX_ENFORCE_NO_NATIVE
          '';
        };
      }
    );
}
