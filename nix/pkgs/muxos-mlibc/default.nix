{ muxos, hello }:

muxos.overrideAttrs (old: {
  pname = "muxos-mlibc";
  buildPhase = ''
    make USER_ELF=${hello}/hello.elf
  '';
})
