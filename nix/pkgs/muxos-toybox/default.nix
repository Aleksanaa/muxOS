{ muxos, toybox }:

# Same kernel, with toybox's standalone `ls` embedded as userland and its
# arguments supplied as the initial argv.
muxos.overrideAttrs (old: {
  pname = "muxos-toybox";
  buildPhase = ''
    make USER_ELF=${toybox}/bin/ls USER_ARGV="ls /"
  '';
})
