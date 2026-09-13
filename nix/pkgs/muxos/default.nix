{ stdenv, lib, buildTools, src, programs ? [], applets ? [] }:

let
  # Each program is embedded in the kernel image and copied into /bin at boot.
  programsArg = lib.concatStringsSep " " (
    map (p: "${p.name}=${p.path}") programs
  );
  # Names hardlinked to the multicall "toybox" binary in /bin.
  appletsArg = lib.concatStringsSep " " applets;
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
    make \
      ${lib.optionalString (programsArg != "") "PROGRAMS=\"${programsArg}\""} \
      ${lib.optionalString (appletsArg != "") "APPLETS=\"${appletsArg}\""}
  '';

  installPhase = ''
    mkdir -p "$out"
    cp build/muxos.iso "$out/muxos.iso"
  '';
}
