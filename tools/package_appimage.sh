#!/bin/sh
# Packs the Linux launcher's ZIP (tools/package_linux.sh) into one AppImage for the Steam Deck and
# other x86_64 Linux: dist/CoreOfLight-<version>-x86_64.AppImage. Mark it executable and start it,
# or add it to Steam as a non-Steam game.
#
#   tools/package_appimage.sh dist/CoreOfLight-linux-x86_64-<version>.zip
#
# The same files as the ZIP, without portable.txt: an AppImage runs from a read-only mount, so it
# doesn't update itself; a new version is a new AppImage. The launcher sees $APPIMAGE and points
# its application menu entry at the AppImage file (desktop_entries.cpp).
#
# Needs appimagetool: $APPIMAGETOOL, else the pinned release is downloaded into dist/tools.
set -eu
zip=${1:?usage: package_appimage.sh <the Linux ZIP>}
here=$(cd "$(dirname "$0")/.." && pwd)
version=$(basename "$zip" .zip | sed 's/^CoreOfLight-linux-x86_64-//')
tool_version=1.9.1

tool=${APPIMAGETOOL:-}
if [ -z "$tool" ]; then
  tool="$here/dist/tools/appimagetool-$tool_version-x86_64.AppImage"
  if [ ! -x "$tool" ]; then
    mkdir -p "$(dirname "$tool")"
    curl -fsSL -o "$tool" \
      "https://github.com/AppImage/appimagetool/releases/download/$tool_version/appimagetool-x86_64.AppImage"
    chmod +x "$tool"
  fi
fi

appdir="$here/dist/appimage/CoreOfLight.AppDir"
rm -rf "$here/dist/appimage"
mkdir -p "$appdir"
python3 -c 'import sys, zipfile; zipfile.ZipFile(sys.argv[1]).extractall(sys.argv[2])' "$zip" "$appdir"
rm -f "$appdir/portable.txt"
chmod +x "$appdir/coreoflight"

cat > "$appdir/AppRun" <<'EOF'
#!/bin/sh
here=$(dirname "$(readlink -f "$0")")
exec "$here/coreoflight" "$@"
EOF
chmod +x "$appdir/AppRun"

cat > "$appdir/coreoflight.desktop" <<'EOF'
[Desktop Entry]
Type=Application
Name=Skylanders: Core of Light
Comment=The launcher for the recompiled Skylanders games
Exec=coreoflight
Icon=coreoflight
Categories=Game;
Terminal=false
EOF
# The icon is in the ZIP (package_linux.sh); appimagetool wants it at the top as well.
cp "$appdir/coreoflight.png" "$appdir/.DirIcon"

out="$here/dist/CoreOfLight-$version-x86_64.AppImage"
rm -f "$out"
# Without FUSE (containers, CI) appimagetool runs extracted.
APPIMAGE_EXTRACT_AND_RUN=1 ARCH=x86_64 "$tool" --no-appstream "$appdir" "$out"
echo "packed: $out"
ls -l "$out"
