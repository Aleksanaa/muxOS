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
  # Filesystem, text and process applets worth having as standalone binaries.
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
    # text processing
    "grep"
    "sed"
    "head"
    "tail"
    "sort"
    "uniq"
    "cut"
    "tr"
    "tee"
    "cmp"
    "seq"
    "yes"
    "basename"
    "dirname"
    "printf"
    "rev"
    # filesystem helpers
    "chmod"
    "stat"
    "du"
    "realpath"
    "readlink"
    "mktemp"
    # process / environment
    "kill"
    "sleep"
    "env"
    "test"
    "xargs"
    "which"
    "timeout"
    "uname"
    "id"
    "whoami"
    # more text / data
    "expr"
    "date"
    "od"
    "strings"
    "split"
    "comm"
    "paste"
    "tac"
    "nl"
    "fold"
    "base64"
    "md5sum"
    "sha256sum"
    "diff"
    "file"
    "more"
    # archives / disk
    "tar"
    "gzip"
    "dd"
    "install"
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

  # Build one multicall "toybox" binary that dispatches on argv[0].  The kernel
  # embeds it once and creates the per-applet /bin entries as hardlinks, which
  # keeps the image small (a per-applet build repeats the runtime/lib in each
  # binary).  The enabled-applet set is chosen the same way scripts/single.sh
  # does it, but the multiplexer is kept instead of disabled.
  buildPhase = ''
    runHook preBuild
    mkdir -p generated
    export KCONFIG_CONFIG=.config
    make allnoconfig
    sed -i 's/# CONFIG_TOYBOX is not set/CONFIG_TOYBOX=y/' .config

    for i in ${lib.concatStringsSep " " toys}; do
      TOYFILE="$(grep -l "TOY($i[ ,]" toys/*/*.c | head -1)"
      NAME="$(echo "$i" | tr a-z- A-Z_)"
      DEPENDS="$({ sed -n "/^config *$i\$/,/^\$/{s/^[ \t]*depends on //;T;s/[!][A-Z0-9_]*//g;s/ *&& */|/g;p}" "$TOYFILE"; } | xargs | tr ' ' '|')"
      sed -ri -e "s/# (CONFIG_($NAME|''${NAME}_.*''${DEPENDS:+|$DEPENDS})) is not set/\1=y/" .config
    done

    make oldconfig < /dev/null
    export OUTNAME=toybox
    bash scripts/make.sh
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    mkdir -p "$out/bin"
    cp toybox "$out/bin/toybox"
    chmod u+w "$out/bin/toybox"
    ${strip} -s "$out/bin/toybox"
    runHook postInstall
  '';

  passthru = { inherit toys; };
}
