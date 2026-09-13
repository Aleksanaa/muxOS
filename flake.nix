{
  description = "muxOS — a minimal 32-bit x86 hobby kernel";

  inputs.nixpkgs.url = "git+https://mirrors.tuna.tsinghua.edu.cn/git/nixpkgs.git?ref=nixos-unstable&shallow=1";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      cross = pkgs.pkgsCross.i686-embedded;
      # mlibc is built with a hosted i686-linux cross compiler, as its porting
      # guide recommends (bare-metal GCC's headers/types disagree with mlibc).
      linuxCross = pkgs.pkgsCross.gnu32;

      buildTools = pkgs.callPackage ./nix/build-tools.nix { inherit cross; };

      mlibc = pkgs.callPackage ./nix/pkgs/mlibc {
        cross = linuxCross;
        mlibcPort = ./ports/mlibc/sysdeps/muxos;
        crossFile = ./ports/mlibc/muxos.cross-file;
      };

      hello = pkgs.callPackage ./nix/pkgs/hello {
        cross = linuxCross;
        inherit mlibc;
        src = ./ports/mlibc;
        helloLd = ./ports/mlibc/hello.ld;
      };

      toybox = pkgs.callPackage ./nix/pkgs/toybox {
        cross = linuxCross;
        inherit mlibc;
        helloLd = ./ports/mlibc/hello.ld;
      };

      mrsh = pkgs.callPackage ./nix/pkgs/mrsh {
        cross = linuxCross;
        inherit mlibc;
        helloLd = ./ports/mlibc/hello.ld;
      };

      muxos = pkgs.callPackage ./nix/pkgs/muxos {
        inherit buildTools;
        src = self;
        programs =
          map (t: {
            name = t;
            path = "${toybox}/bin/${t}";
          }) toybox.toys
          ++ [
            {
              name = "sh";
              path = "${mrsh}/bin/sh";
            }
          ];
      };

      runQemu = iso: pkgs.writeShellScriptBin "muxos-run" ''
        exec ${pkgs.qemu}/bin/qemu-system-i386 \
          -m 512M \
          -cdrom ${iso}/muxos.iso \
          "$@"
      '';
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = buildTools ++ [
          linuxCross.stdenv.cc
          linuxCross.binutils
          pkgs.qemu
          pkgs.meson
          pkgs.ninja
          pkgs.pkg-config
        ];
        shellHook = ''
          export QEMUFLAGS="''${QEMUFLAGS:--display gtk}"
        '';
      };

      packages.${system} = {
        default = muxos;
        mlibc = mlibc;
        hello = hello;
        toybox = toybox;
        mrsh = mrsh;
      };

      apps.${system} = {
        default = {
          type = "app";
          program = "${runQemu muxos}/bin/muxos-run";
        };
      };
    };
}
