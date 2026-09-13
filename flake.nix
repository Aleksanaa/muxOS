{
  description = "muxOS — a minimal 32-bit x86 hobby kernel";

  inputs.nixpkgs.url = "git+https://mirrors.tuna.tsinghua.edu.cn/git/nixpkgs.git?ref=nixos-unstable&shallow=1";

  outputs =
    { self, nixpkgs }:
    let
      system = "x86_64-linux";
      pkgs = nixpkgs.legacyPackages.${system};
      cross = pkgs.pkgsCross.i686-embedded;

      buildTools = with pkgs; [
        cross.stdenv.cc
        cross.binutils
        nasm
        grub2
        xorriso
        mtools
        gnumake
      ];

      muxos = pkgs.stdenv.mkDerivation {
        pname = "muxos";
        version = "0.0.1";
        src = self;

        nativeBuildInputs = buildTools;

        preBuild = ''
          rm -rf build
        '';

        buildPhase = ''
          make
        '';

        installPhase = ''
          mkdir -p "$out"
          cp build/muxos.iso "$out/muxos.iso"
        '';
      };

      runQemu = pkgs.writeShellScriptBin "muxos-run" ''
        exec ${pkgs.qemu}/bin/qemu-system-i386 \
          -m 256M \
          -cdrom ${muxos}/muxos.iso \
          "$@"
      '';
    in
    {
      devShells.${system}.default = pkgs.mkShell {
        packages = buildTools ++ [ pkgs.qemu ];
        shellHook = ''
          export QEMUFLAGS="''${QEMUFLAGS:--display gtk}"
        '';
      };

      packages.${system}.default = muxos;

      apps.${system}.default = {
        type = "app";
        program = "${runQemu}/bin/muxos-run";
      };
    };
}
