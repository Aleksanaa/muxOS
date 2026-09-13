{
  lib,
  stdenv,
  fetchFromGitHub,
  cross,
  mlibc,
  helloLd,
}:

let
  cc = "${cross.stdenv.cc.targetPrefix}gcc";
  strip = "${cross.stdenv.cc.targetPrefix}strip";

  mrshSrc = fetchFromGitHub {
    owner = "emersion";
    repo = "mrsh";
    rev = "4c81598721bc5eeb28f9faa818b3102d0471b7f6";
    hash = "sha256-k/NdDXotLVrazqqBa13//gk68bZ19nIXs8rUfJMWhNs=";
  };
in
stdenv.mkDerivation {
  pname = "mrsh-muxos";
  version = "0.0.0";

  src = mrshSrc;

  nativeBuildInputs = [
    cross.stdenv.cc
    cross.binutils
  ];

  dontConfigure = true;
  dontPatchELF = true;
  dontStrip = true;

  buildPhase = ''
    runHook preBuild
    gccinc="$(${cc} -print-file-name=include)"
    CFLAGS="-m32 -ffreestanding -fno-stack-protector -D_GNU_SOURCE \
      -nostdinc -isystem ${mlibc}/usr/include -isystem $gccinc -Iinclude"
    mkdir -p obj
    for f in $(find . -name '*.c' -not -path './test/*' \
        -not -path './example/*' -not -name 'readline.c' | sort); do
      o="obj/$(echo "$f" | sed 's|^\./||; s|/|_|g; s|\.c$|.o|')"
      ${cc} $CFLAGS -c "$f" -o "$o"
    done
    # No readline/editline: frontend/basic.c provides the POSIX line reader.
    ${cc} -m32 -nostdlib -static -T ${helloLd} -o sh \
      ${mlibc}/usr/lib/crt1.o obj/*.o -L${mlibc}/usr/lib -lc -lgcc
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin"
    cp sh "$out/bin/sh"
    chmod u+w "$out/bin/sh"
    ${strip} -s "$out/bin/sh"
    runHook postInstall
  '';
}
