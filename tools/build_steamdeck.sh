#!/bin/sh
# The Linux launcher for the Steam Deck (and any other x86_64 Linux): built in Valve's Steam
# Runtime 3 "sniper" SDK, so it runs on SteamOS's older C library, then packed by package_linux.sh
# into dist/CoreOfLight-linux-x86_64-<version>.zip.
#
#   tools/build_steamdeck.sh            (needs Docker or Podman; the SDK image is about 1.5 GB)
#
# A build made on a desktop distribution needs that distribution's newer C library: it doesn't
# start on a Deck.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
image=${STEAMRT_IMAGE:-registry.gitlab.steamos.cloud/steamrt/sniper/sdk:latest}
engine=${CONTAINER_ENGINE:-}
if [ -z "$engine" ]; then
  if command -v podman >/dev/null 2>&1; then engine=podman; else engine=docker; fi
fi
"$engine" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$here:/src" -w /src "$image" sh -euc '
  # The SDK has several compilers; the newest GCC it brings.
  for v in 14 13 12; do
    if command -v g++-$v >/dev/null 2>&1; then export CC=gcc-$v CXX=g++-$v; break; fi
  done
  echo "compiler: ${CXX:-c++}"
  cmake --preset steamdeck
  cmake --build --preset steamdeck
  tools/package_linux.sh out/build/steamdeck
'
