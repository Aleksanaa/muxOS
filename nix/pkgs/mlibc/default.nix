{
  stdenv,
  meson,
  ninja,
  pkg-config,
  cross,
  fetchFromGitHub,
  mlibcPort,
  crossFile,
}:

let
  fetch = args: fetchFromGitHub args;

  mlibcSrc = fetch {
    owner = "managarm";
    repo = "mlibc";
    rev = "v7.0.0";
    hash = "sha256-e4YjosGDI2CWkGeih0HG69yPJa+sKAReTQ87lgzBTzg=";
  };
  frigg = fetch {
    owner = "managarm";
    repo = "frigg";
    rev = "b0dbea66bc19f7c5546f0039a3be842feb02678c";
    hash = "sha256-DfNNvRGQFdfzoO3r+peRpnzlGmuceA5VZgHQoK5rk28=";
  };
  libsmarter = fetch {
    owner = "managarm";
    repo = "libsmarter";
    rev = "f7d061bc37d485418344452c7ceb28d5df3ba85d";
    hash = "sha256-K2K2Vya8uOtcVBhogbaS7xN8KKMiWvStHQD7MWF3EWk=";
  };
  bragi = fetch {
    owner = "managarm";
    repo = "bragi";
    rev = "523b86efac124b0d749eed201df0c7ea9f87ee17";
    hash = "sha256-uCSijxsgachNDJ6i9LhCp2nz0fS2nd4zDqD3EyNcdng=";
  };
  freestndCHdrs = fetch {
    owner = "osdev0";
    repo = "freestnd-c-hdrs";
    rev = "d33711241b46ecb8f2ad33927fcefdcb3ac0162e";
    hash = "sha256-gi+ZNmZvzYicRc/NZONFC2P984EXcyp7nUtT6vXaJ68=";
  };
  freestndCxxHdrs = fetch {
    owner = "osdev0";
    repo = "freestnd-cxx-hdrs";
    rev = "a6b351e0ab3e74e5789b01fa1447e4cd62373da7";
    hash = "sha256-sDXHMP/xTuL+DtaJgyxl322IWIXXcqRUbtJRMvYUmZY=";
  };

  abiBits = [
    "access"
    "aio"
    "auxv"
    "blkcnt_t"
    "blksize_t"
    "clockid_t"
    "dev_t"
    "errno"
    "fcntl"
    "fd_set"
    "fsblkcnt_t"
    "fsfilcnt_t"
    "gid_t"
    "in"
    "ino_t"
    "ipc"
    "limits"
    "mode_t"
    "mqueue"
    "msg"
    "nlink_t"
    "pid_t"
    "poll"
    "resource"
    "rlim_t"
    "sa_family_t"
    "sched_param"
    "seek-whence"
    "sem"
    "shm"
    "sig-limits"
    "sigevent"
    "signal"
    "sigset_t"
    "sigval"
    "sockaddr_storage"
    "socket"
    "socklen_t"
    "stat"
    "statvfs"
    "suseconds_t"
    "termios"
    "time"
    "uid_t"
    "utmp-defines"
    "utmpx"
    "utsname"
    "vm-flags"
    "wait"
  ];
in
stdenv.mkDerivation {
  pname = "mlibc-muxos";
  version = "7.0.0";

  src = mlibcSrc;

  nativeBuildInputs = [
    meson
    ninja
    pkg-config
    cross.stdenv.cc
    cross.binutils
  ];

  postPatch = ''
    rm -rf sysdeps/muxos
    cp -r ${mlibcPort} sysdeps/muxos
    chmod -R u+w sysdeps/muxos

    mkdir -p sysdeps/muxos/include/abi-bits
    for f in ${toString abiBits}; do
      cp "abis/linux/$f.h" "sysdeps/muxos/include/abi-bits/$f.h"
    done

    rm -rf subprojects/frigg subprojects/libsmarter subprojects/bragi \
           subprojects/freestnd-c-hdrs subprojects/freestnd-cxx-hdrs
    cp -r ${frigg} subprojects/frigg
    cp -r ${libsmarter} subprojects/libsmarter
    cp -r ${bragi} subprojects/bragi
    cp -r ${freestndCHdrs} subprojects/freestnd-c-hdrs
    cp -r ${freestndCxxHdrs} subprojects/freestnd-cxx-hdrs
    chmod -R u+w subprojects
    cp subprojects/packagefiles/freestnd-c-hdrs/meson.build \
       subprojects/freestnd-c-hdrs/meson.build
    cp subprojects/packagefiles/freestnd-cxx-hdrs/meson.build \
       subprojects/freestnd-cxx-hdrs/meson.build

    substituteInPlace meson.build \
      --replace-fail "elif host_machine.system() == 'demo'" \
      "elif host_machine.system() == 'muxos'
	subdir('sysdeps/muxos')
elif host_machine.system() == 'demo'"
  '';

  configurePhase = ''
    runHook preConfigure
    meson setup build \
      --cross-file ${crossFile} \
      --prefix=/usr \
      -Ddefault_library=static \
      -Duse_freestnd_hdrs=enabled
    runHook postConfigure
  '';

  buildPhase = ''
    runHook preBuild
    ninja -C build
    runHook postBuild
  '';

  installPhase = ''
    runHook preInstall
    DESTDIR=$out ninja -C build install
    runHook postInstall
  '';
}
