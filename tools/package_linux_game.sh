#!/bin/sh
# Packs a Linux build of one game into the archive Core of Light installs on Linux
# (games.h SetupPrefix: <Repo>-linux-x86_64-<version>.tar.gz), its files at the archive's top:
# the program and the libraries next to it (its runpath is $ORIGIN). No disc files (rom/) and no
# settings file: the launcher sets up the disc itself and a game makes its own settings.
#
#   tools/package_linux_game.sh <game checkout> [out folder]
#   e.g. tools/package_linux_game.sh ~/Skylanders/SSARecomp/SSARecomp ~/Applications/CoreOfLight/games
#
# The game must have been built with its linux-amd64-release preset. The archive holds code
# recompiled from your own disc: it is for your own PCs, never for a release or anyone else.
set -eu
repo_dir=$(cd "${1:?usage: package_linux_game.sh <game checkout> [out folder]}" && pwd)
out=${2:-$(cd "$(dirname "$0")/.." && pwd)/dist}
build="$repo_dir/out/build/linux-amd64-release"
repo=$(basename "$repo_dir")
version=$(sed -n 's/.*GameVersion() { return "\(.*\)"; }.*/\1/p' "$build"/*_build_version.cpp)
exe=$(find "$build" -maxdepth 1 -type f -name '*recompiled' -perm -u+x | head -1)
if [ -z "$version" ] || [ -z "$exe" ]; then
  echo "no Linux build in $build (cmake --build --preset linux-amd64-release)" >&2
  exit 1
fi
stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT
cp "$exe" "$stage/"
cp "$build"/*.so "$stage/" 2>/dev/null || true
cp "$build"/rexglue-out/*.so "$stage/"
strip --strip-unneeded "$stage"/* 2>/dev/null || true
mkdir -p "$out"
archive="$out/$repo-linux-x86_64-$version.tar.gz"
tar -czf "$archive" -C "$stage" .
echo "packed: $archive"
ls -l "$archive"
