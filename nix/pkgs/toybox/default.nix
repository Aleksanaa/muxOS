{
  stdenv,
  lib,
  fetchFromGitHub,
  gnumake,
  gnused,
  gnugrep,
  gawk,
  findutils,
  bash,
  cross,
  mlibc,
  helloLd,
}:

let
  cc = "${cross.stdenv.cc.targetPrefix}gcc";
  ar = "${cross.stdenv.cc.targetPrefix}ar";
  strip = "${cross.stdenv.cc.targetPrefix}strip";

  toyboxSrc = fetchFromGitHub {
    owner = "landley";
    repo = "toybox";
    rev = "0.8.14";
    hash = "sha256-46iKwUSIQ4M9ZL86e4rY4hGcz8y06HZMC0mvNp3jR1s=";
  };
  # Filesystem-related applets worth having as standalone binaries.
  toys = [
    "hello"
    "echo"
    "ls"
    "cat"
    "cp"
    "mv"
    "rm"
    "mkdir"
    "rmdir"
    "touch"
    "find"
    "wc"
    "pwd"
    "ln"
    "true"
    "false"
  ];
in
stdenv.mkDerivation {
  pname = "toybox-muxos";
  version = "0.8.14";

  src = toyboxSrc;

  nativeBuildInputs = [
    gnumake
    gnused
    gnugrep
    gawk
    findutils
    bash
    cross.stdenv.cc
    cross.binutils
  ];

  dontConfigure = true;

  postPatch = ''
    chmod +x scripts/*.sh configure
    patchShebangs scripts configure

    substituteInPlace lib/portability.c \
      --replace-fail $'#else\n#error\n#endif' $'#else\n  return 0;\n#endif'
  '';

  preBuild = ''
    export CC="${cc}"
    export HOSTCC="cc"
    gccinc="$($CC -print-file-name=include)"
    export CFLAGS="-m32 -ffreestanding -fno-stack-protector -U__linux__ -nostdinc -isystem ${mlibc}/usr/include -isystem ${./compat-include} -isystem $gccinc -include ${./compat.h}"
    export LDFLAGS="-m32 -nostdlib -static -T ${helloLd} ${mlibc}/usr/lib/crt1.o -L${mlibc}/usr/lib -lc -lgcc"
  '';

  buildPhase = ''
    runHook preBuild
    mkdir -p generated
    scripts/single.sh ${lib.concatStringsSep " " toys}
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin"
    for t in ${lib.concatStringsSep " " toys}; do
      cp "$t" "$out/bin/$t"
      chmod u+w "$out/bin/$t"
      ${strip} -s "$out/bin/$t"
    done
    runHook postInstall
  '';

  passthru = { inherit toys; };
}
