{
  stdenv,
  fetchFromGitHub,
  cross,
  mlibc,
  helloLd,
}:

let
  cc = "${cross.stdenv.cc.targetPrefix}gcc";
  strip = "${cross.stdenv.cc.targetPrefix}strip";
in
stdenv.mkDerivation {
  pname = "sfm-muxos";
  version = "0.5";

  src = fetchFromGitHub {
    owner = "afify";
    repo = "sfm";
    rev = "f1f1197142421d3f727dc109a5910129d0bcb0b0";
    hash = "sha256-9hdEO4nJ1pINQCzOjA0sXm/BBdxA5JbHpY60lg6ljjA=";
  };

  # Disable the inotify/kqueue + pthread filesystem watcher (muxOS has neither);
  # panes are refreshed with the bound key instead.  See muxos.patch.
  patches = [ ./muxos.patch ];

  nativeBuildInputs = [
    cross.stdenv.cc
    cross.binutils
  ];

  dontConfigure = true;
  dontPatchELF = true;
  dontStrip = true;

  buildPhase = ''
    runHook preBuild
    cp config.def.h config.h
    gccinc="$(${cc} -print-file-name=include)"
    CFLAGS="-m32 -O2 -ffreestanding -fno-stack-protector -D_GNU_SOURCE \
      -DSFM_NO_WATCH -DVERSION=\"0.5\" \
      -nostdinc -isystem ${mlibc}/usr/include -isystem ${./compat-include} \
      -isystem $gccinc"
    ${cc} $CFLAGS -c sfm.c -o sfm.o
    ${cc} -m32 -nostdlib -static -T ${helloLd} -o sfm \
      ${mlibc}/usr/lib/crt1.o sfm.o -L${mlibc}/usr/lib -lc -lgcc
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin"
    cp sfm "$out/bin/sfm"
    chmod u+w "$out/bin/sfm"
    ${strip} -s "$out/bin/sfm"
    runHook postInstall
  '';
}
