#!/usr/bin/env bash
# Packs AppDir/ (from build-linux-release-into-appdir.sh) with its Qt libraries into Doodlebold-<version>-x86_64.AppImage.
# Needs QT_ROOT_DIR, the Qt that the release was built with.
set -euo pipefail
cd "$(dirname "$0")/.."

# linuxdeploy at pinned releases, not "continuous", so the package doesn't change under us.
LINUXDEPLOY_URL=https://github.com/linuxdeploy/linuxdeploy/releases/download/1-alpha-20251107-1/linuxdeploy-x86_64.AppImage
LINUXDEPLOY_QT_URL=https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/1-alpha-20250213-1/linuxdeploy-plugin-qt-x86_64.AppImage
ICON=AppDir/usr/share/icons/hicolor/256x256/apps/doodlebold.png

export APPIMAGE_EXTRACT_AND_RUN=1
export QMAKE="$QT_ROOT_DIR/bin/qmake"
export EXTRA_PLATFORM_PLUGINS="libqwayland-generic.so;libqwayland-egl.so"
export EXTRA_QT_PLUGINS="wayland-decoration-client;wayland-graphics-integration-client;wayland-shell-integration"

curl -sSLf -o linuxdeploy "$LINUXDEPLOY_URL"
curl -sSLf -o linuxdeploy-plugin-qt "$LINUXDEPLOY_QT_URL"
chmod +x linuxdeploy linuxdeploy-plugin-qt

version=$(sed -n 's/^project(Doodlebold VERSION \([0-9.]*\).*/\1/p' CMakeLists.txt)
export LDAI_OUTPUT="Doodlebold-$version-x86_64.AppImage"
cp "$ICON" AppDir/doodlebold.png  # appimagetool makes .DirIcon from it
./linuxdeploy --appdir AppDir --plugin qt --desktop-file AppDir/usr/share/applications/doodlebold.desktop \
  --icon-file "$ICON" --output appimage
