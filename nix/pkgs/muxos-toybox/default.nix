{ muxos, toybox }:

# Same kernel, with toybox's standalone `echo` embedded as userland and its
# arguments supplied as the initial argv.
muxos.overrideAttrs (old: {
  pname = "muxos-toybox";
  buildPhase = ''
    make USER_ELF=${toybox}/bin/echo USER_ARGV="echo hello from toybox"
  '';
})
