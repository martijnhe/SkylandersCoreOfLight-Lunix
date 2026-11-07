#!/bin/sh
# Packs a Linux build of the launcher into dist/CoreOfLight-linux-x86_64-<version>.zip, with
# its files at the ZIP's top, as the portable update puts them next to the running launcher:
#
#   coreoflight        the program (it finds lib/ next to it: its RPATH is $ORIGIN/lib)
#   fonts/             Noto Sans (SIL OFL)
#   lib/               the libraries a system may lack (libcurl, OpenSSL and theirs)
#   portable.txt       so the launcher updates itself from this ZIP (portable.h)
#   coreoflight.png    its icon for the application menu (desktop_entries.h)
#   README-linux.txt
#
#   tools/package_linux.sh <build folder>      (tools/build_steamdeck.sh calls it)
#
# The graphics, sound, input and display libraries stay the system's (they belong to its drivers),
# as does the C library.
set -eu
build=${1:?usage: package_linux.sh <build folder>}
here=$(cd "$(dirname "$0")/.." && pwd)
version=$(sed -n 's/^set(COREOFLIGHT_VERSION "\(.*\)")$/\1/p' "$here/CMakeLists.txt")
stage="$here/dist/linux/CoreOfLight"
rm -rf "$here/dist/linux"
mkdir -p "$stage/lib" "$stage/fonts"
cp "$build/coreoflight" "$stage/"
strip "$stage/coreoflight" 2>/dev/null || true
cp "$here/fonts/"* "$stage/fonts/"
printf 'Portable Core of Light: it updates itself from the release ZIP.\n' > "$stage/portable.txt"
cp "$here/installer/linux/README-linux.txt" "$stage/"
# The launcher's own icon for its menu entry (desktop_entries.h): the 256 px picture in its .ico.
python3 - "$here/src/coreoflight.ico" "$stage/coreoflight.png" <<'PY'
import struct, sys
data = open(sys.argv[1], "rb").read()
count = struct.unpack("<HHH", data[:6])[2]
best = max(range(count), key=lambda i: data[6 + 16 * i] or 256)
size, offset = struct.unpack("<II", data[6 + 16 * best + 8:6 + 16 * best + 16])
open(sys.argv[2], "wb").write(data[offset:offset + size])
PY

# The libraries the program needs that aren't the system's own: copied with what they need.
keep_system='^(linux-vdso|ld-linux|libc|libm|libdl|libpthread|librt|libresolv|libutil|libGL|libGLX|libGLdispatch|libEGL|libOpenGL|libgbm|libdrm|libX|libxcb|libwayland|libxkbcommon|libasound|libpulse|libpipewire|libdbus|libudev|libsystemd|libgcc_s|libstdc\+\+)[.-]'
ldd "$stage/coreoflight" | awk '/=> \// {print $1, $3}' | while read -r name path; do
  if printf '%s\n' "$name" | grep -Eq "$keep_system"; then continue; fi
  cp -L "$path" "$stage/lib/$name"
done
missing=$(LD_LIBRARY_PATH="$stage/lib" ldd "$stage/coreoflight" | grep 'not found' || true)
if [ -n "$missing" ]; then
  echo "missing libraries:" >&2
  echo "$missing" >&2
  exit 1
fi

zip="$here/dist/CoreOfLight-linux-x86_64-$version.zip"
rm -f "$zip"
python3 - "$here/dist/linux" "$zip" <<'PY'
import os, sys, zipfile
root, out = sys.argv[1], sys.argv[2]
with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED) as z:
    for folder, _, files in os.walk(root):
        for name in sorted(files):
            path = os.path.join(folder, name)
            info = zipfile.ZipInfo.from_file(path, os.path.relpath(path, os.path.join(root, "CoreOfLight")))
            info.compress_type = zipfile.ZIP_DEFLATED
            with open(path, "rb") as f:
                z.writestr(info, f.read())
PY
echo "packed: $zip"
ls -l "$zip"
