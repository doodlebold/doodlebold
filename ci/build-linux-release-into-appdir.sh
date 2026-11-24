#!/usr/bin/env bash
# Builds the release and installs it into AppDir/, the tree package-appimage.sh packs.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset linux-release
cmake --build --preset linux-release
DESTDIR="$PWD/AppDir" cmake --install build-linux-release
