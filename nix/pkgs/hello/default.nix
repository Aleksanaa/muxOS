{ stdenv, cross, mlibc, src, helloLd }:

let
  cc = "${cross.stdenv.cc.targetPrefix}gcc";
in
stdenv.mkDerivation {
  pname = "muxos-hello";
  version = "1";

  inherit src;
  nativeBuildInputs = [ cross.stdenv.cc cross.binutils ];

  buildPhase = ''
    ${cc} -m32 -ffreestanding -fno-stack-protector \
      -I ${mlibc}/usr/include -c hello.c -o hello.o
    ${cc} -m32 -nostdlib -static \
      -T ${helloLd} -o hello.elf \
      ${mlibc}/usr/lib/crt1.o hello.o \
      -L ${mlibc}/usr/lib -lc -lgcc
  '';

  installPhase = ''
    mkdir -p "$out"
    cp hello.elf "$out/hello.elf"
  '';
}
