{
  description = "mupager - Terminal document viewer";

  inputs = {
    nixpkgs.url = "github:NixOS/nixpkgs/nixos-unstable";
    flake-utils.url = "github:numtide/flake-utils";

    # CPM dependencies (pinned to match CMakeLists.txt)
    doctest = {
      url = "github:doctest/doctest/v2.4.12";
      flake = false;
    };
    tomlplusplus = {
      url = "github:marzer/tomlplusplus/v3.4.0";
      flake = false;
    };
    cli11 = {
      url = "github:CLIUtils/CLI11/v2.4.2";
      flake = false;
    };
    spdlog = {
      url = "github:gabime/spdlog/v1.15.0";
      flake = false;
    };
    msgpack-cxx = {
      url = "github:msgpack/msgpack-c/cpp-7.0.0";
      flake = false;
    };
  };

  outputs = {
    self,
    nixpkgs,
    flake-utils,
    doctest,
    tomlplusplus,
    cli11,
    spdlog,
    msgpack-cxx,
  }:
    flake-utils.lib.eachDefaultSystem (
      system: let
        pkgs = import nixpkgs {
          inherit system;
        };
        isLinux = pkgs.stdenv.isLinux;

        cpm-cmake = pkgs.fetchurl {
          url = "https://github.com/cpm-cmake/CPM.cmake/releases/download/v0.42.0/CPM.cmake";
          sha256 = "12nf9nz1ywy53kqq39c4yk9c7krfd0p381iyk0bli96v8byb8810";
        };

        # Wrap clangd with --query-driver so it discovers Nix compiler system includes
        wrapped-clangd = pkgs.writeShellScriptBin "clangd" ''
          exec ${pkgs.clang-tools}/bin/clangd --query-driver="/nix/store/*/bin/*" "$@"
        '';
      in {
        packages.default = pkgs.clangStdenv.mkDerivation {
          pname = "mupager";
          version = "0.0.0";
          src = pkgs.lib.cleanSource ./.;
          nativeBuildInputs = with pkgs; [cmake ninja pkg-config];
          buildInputs = with pkgs; [mupdf ncurses];

          preConfigure = ''
            export CPM_SOURCE_CACHE=$TMPDIR/cpm-cache
            mkdir -p $CPM_SOURCE_CACHE/cpm
            cp ${cpm-cmake} $CPM_SOURCE_CACHE/cpm/CPM_0.42.0.cmake
          '';

          cmakeFlags = [
            "-DCPM_doctest_SOURCE=${doctest}"
            "-DCPM_tomlplusplus_SOURCE=${tomlplusplus}"
            "-DCPM_CLI11_SOURCE=${cli11}"
            "-DCPM_spdlog_SOURCE=${spdlog}"
            "-DCPM_msgpack-cxx_SOURCE=${msgpack-cxx}"
          ];

          doCheck = true;
          checkPhase = ''ctest -j$NIX_BUILD_CORES --output-on-failure'';
          meta = with pkgs.lib; {
            description = "Terminal document viewer with pixel-perfect rendering";
            license = licenses.mit;
            platforms = platforms.unix;
            mainProgram = "mupager";
          };
        };

        devShells.default = pkgs.mkShell {
          name = "mupager";

          packages = with pkgs;
            [
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
              ncurses
            ]
            ++ lib.optionals isLinux [
              # Linux-only tools
              gcc14
              gdb
              valgrind
            ];

          shellHook = ''
            export CC=clang
            export CXX=clang++
            unset NIX_ENFORCE_NO_NATIVE
            export MallocNanoZone=0
            export PATH="${wrapped-clangd}/bin:$PATH"
          '';
        };
      }
    );
}
