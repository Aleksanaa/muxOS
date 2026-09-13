{ pkgs, cross }:
with pkgs; [
  cross.stdenv.cc
  cross.binutils
  nasm
  grub2
  xorriso
  mtools
  gnumake
]
