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

  neatviSrc = fetchFromGitHub {
    owner = "aligrudi";
    repo = "neatvi";
    rev = "02dd06d28b9bfe95a07cee72d7ab61789e5defee";
    hash = "sha256-EIBa99WhC39Wl3KHGYyoBpX/VrbpXqvAYE4uD4SNDkY=";
  };
in
stdenv.mkDerivation {
  pname = "neatvi-muxos";
  version = "0.0.0";

  src = neatviSrc;

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
    CFLAGS="-m32 -O2 -ffreestanding -fno-stack-protector -D_GNU_SOURCE \
      -nostdinc -isystem ${mlibc}/usr/include -isystem ${./compat-include} \
      -isystem $gccinc"
    mkdir -p obj
    # Everything except the `stag` (tag query) tool, which has its own main().
    for f in $(find . -name '*.c' -not -path './test/*' -not -name 'stag.c' | sort); do
      o="obj/$(echo "$f" | sed 's|^\./||; s|/|_|g; s|\.c$|.o|')"
      ${cc} $CFLAGS -c "$f" -o "$o"
    done
    ${cc} -m32 -nostdlib -static -T ${helloLd} -o vi \
      ${mlibc}/usr/lib/crt1.o obj/*.o -L${mlibc}/usr/lib -lc -lgcc
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin"
    cp vi "$out/bin/vi"
    chmod u+w "$out/bin/vi"
    ${strip} -s "$out/bin/vi"
    runHook postInstall
  '';
}
