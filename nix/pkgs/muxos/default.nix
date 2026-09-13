{ stdenv, lib, buildTools, src, programs ? [] }:

let
  # Each program is embedded in the kernel image and copied into /bin at boot.
  programsArg = lib.concatStringsSep " " (
    map (p: "${p.name}=${p.path}") programs
  );
in
stdenv.mkDerivation {
  pname = "muxos";
  version = "0.0.1";

  inherit src;
  nativeBuildInputs = buildTools;

  preBuild = ''
    rm -rf build
  '';

  buildPhase = ''
    make ${lib.optionalString (programsArg != "") "PROGRAMS=\"${programsArg}\""}
  '';

  installPhase = ''
    mkdir -p "$out"
    cp build/muxos.iso "$out/muxos.iso"
  '';
}
