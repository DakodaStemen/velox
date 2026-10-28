#!/usr/bin/env bash
# Builds Velox-<version>-x86_64.AppImage from an installed tree.
#   packaging/appimage/build-appimage.sh <build-dir>
# Needs linuxdeploy + linuxdeploy-plugin-qt on PATH (or LINUXDEPLOY=/path) and
# QMAKE pointing at the Qt qmake to bundle (qmake6 on Debian/Ubuntu).
set -euo pipefail
build="${1:?usage: $0 <cmake-build-dir>}"
here="$(cd -- "$(dirname -- "$0")/../.." && pwd)"
appdir="$(mktemp -d)/AppDir"
DESTDIR="$appdir" cmake --install "$build" --prefix /usr
export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
export VERSION="$(sed -n 's/^VERSION *= *//p' "$here/velox.pro" | head -n1 | tr -d ' \r')"
export LINUXDEPLOY_OUTPUT_VERSION="$VERSION"
"${LINUXDEPLOY:-linuxdeploy}" --appdir "$appdir" --plugin qt \
  --desktop-file "$appdir/usr/share/applications/io.github.DakodaStemen.Velox.desktop" \
  --icon-file "$appdir/usr/share/icons/hicolor/scalable/apps/io.github.DakodaStemen.Velox.svg" \
  --output appimage
