{ stdenv, buildTools, src }:

stdenv.mkDerivation {
  pname = "muxos";
  version = "0.0.1";

  inherit src;
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
}
