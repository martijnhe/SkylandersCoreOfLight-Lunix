#!/bin/sh
# A game's Linux build for the Steam Deck (and other distributions with an older C library than the
# build machine's): built in an Ubuntu 24.04 container (glibc 2.39, older than SteamOS 3.6 and up)
# with clang 20 from apt.llvm.org, then packed like tools/package_linux_game.sh does, with the
# container's libstdc++ and libgcc_s next to the program (its runpath is $ORIGIN), so the Deck's own
# versions don't matter. Valve's sniper SDK (tools/build_steamdeck.sh, the launcher) only has
# clang 11, too old for the games' C++23.
#
#   tools/build_game_steamdeck.sh <game checkout> [out folder]
#   e.g. tools/build_game_steamdeck.sh ~/Skylanders/SSARecomp/SSARecomp ~/deck
#
# The checkout needs its submodules and its disc in rom/ (the build makes the icon from it and
# checks default.xex). Builds into out/build/deck-amd64-release, beside the desktop build. The
# archive holds code recompiled from your own disc: it is for your own devices, never for a release
# or anyone else. Needs Docker or Podman.
set -eu
here=$(cd "$(dirname "$0")/.." && pwd)
repo_dir=$(cd "${1:?usage: build_game_steamdeck.sh <game checkout> [out folder]}" && pwd)
out=${2:-$here/dist/steamdeck}
mkdir -p "$out"
out=$(cd "$out" && pwd)
engine=${CONTAINER_ENGINE:-}
if [ -z "$engine" ]; then
  if command -v podman >/dev/null 2>&1; then engine=podman; else engine=docker; fi
fi
image=coreoflight-deck-games:24.04

if ! "$engine" image inspect "$image" >/dev/null 2>&1; then
  "$engine" build -t "$image" - <<'EOF'
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends ca-certificates wget gnupg \
      cmake ninja-build python3 pkg-config git file g++-14 \
      libx11-dev libxext-dev libxrandr-dev libxcursor-dev libxi-dev libxss-dev libxfixes-dev \
      libxkbcommon-dev libwayland-dev wayland-protocols libdecor-0-dev libegl-dev libgl-dev \
      libdrm-dev libgbm-dev libasound2-dev libpulse-dev libpipewire-0.3-dev libudev-dev \
      libdbus-1-dev libusb-1.0-0-dev libibus-1.0-dev \
 && wget -qO- https://apt.llvm.org/llvm-snapshot.gpg.key > /etc/apt/trusted.gpg.d/apt.llvm.org.asc \
 && echo "deb http://apt.llvm.org/noble/ llvm-toolchain-noble-20 main" > /etc/apt/sources.list.d/llvm.list \
 && apt-get update && apt-get install -y --no-install-recommends clang-20 lld-20 \
 && ln -s /usr/bin/ld.lld-20 /usr/local/bin/ld.lld \
 && rm -rf /var/lib/apt/lists/*
EOF
fi

"$engine" run --rm --user "$(id -u):$(id -g)" -e HOME=/tmp -v "$repo_dir:/src" -v "$out:/out" -w /src "$image" sh -euc '
  build=out/build/deck-amd64-release
  cmake --preset linux-amd64-release -B "$build" -DCMAKE_C_COMPILER=clang-20 -DCMAKE_CXX_COMPILER=clang++-20
  cmake --build "$build"
  repo=$(basename "$(git config --get remote.origin.url || echo unknown)" .git)
  version=$(sed -n "s/.*GameVersion() { return \"\(.*\)\"; }.*/\1/p" "$build"/*_build_version.cpp)
  exe=$(find "$build" -maxdepth 1 -type f -name "*recompiled" -perm -u+x | head -1)
  stage=$(mktemp -d)
  cp "$exe" "$stage/"
  cp "$build"/*.so "$stage/" 2>/dev/null || true
  cp "$build"/rexglue-out/*.so "$stage/"
  cp -L /usr/lib/x86_64-linux-gnu/libstdc++.so.6 /usr/lib/x86_64-linux-gnu/libgcc_s.so.1 "$stage/"
  strip --strip-unneeded "$stage"/* 2>/dev/null || true
  tar -czf "/out/$repo-linux-x86_64-$version.tar.gz" -C "$stage" .
  ls -l "/out/$repo-linux-x86_64-$version.tar.gz"
'
