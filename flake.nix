{
  description = "mupager - Terminal document viewer";

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
        pkgs = import nixpkgs {
          inherit system;
        };
        isLinux = pkgs.stdenv.isLinux;

        # Wrap clangd with --query-driver so it discovers Nix compiler system includes
        wrapped-clangd = pkgs.writeShellScriptBin "clangd" ''
          exec ${pkgs.clang-tools}/bin/clangd --query-driver="/nix/store/*/bin/*" "$@"
        '';
      in {
        devShells.default = pkgs.mkShell {
          name = "mupager";

          packages = with pkgs; [
            # Compilers
            clang_19
            llvmPackages_19.bintools

            # Build tools
            cmake
            ninja
            pkg-config

            # C++ formatting / linting / LSP
            clang-tools
            wrapped-clangd
            include-what-you-use

            # Lua formatting / linting
            stylua
            selene

            # Task runner
            just

            # Terminal graphics testing
            kitty

            # Document conversion
            pandoc

            # Libraries
            mupdf
            msgpack-cxx
            boost
            ncurses
          ] ++ lib.optionals isLinux [
            # Linux-only tools
            gcc14
            gdb
            valgrind
          ];

          shellHook = ''
            export CC=clang
            export CXX=clang++
            unset NIX_ENFORCE_NO_NATIVE
            export PATH="${wrapped-clangd}/bin:$PATH"
          '';
        };
      }
    );
}
