#!/usr/bin/env bash
# Installs what the AppImage build needs on Ubuntu 22.04, besides Qt, which the workflow installs.
set -euo pipefail

WAYLAND_PROTOCOLS_VERSION=1.49  # Ubuntu 22.04's own are too old for cursor-shape-v1 and ext-background-effect-v1

sudo apt-get update
sudo apt-get install -y ninja-build meson libjpeg-turbo8-dev libwayland-dev libgl1-mesa-dev libxkbcommon-dev \
  libxcb-cursor0 libfuse2 file

curl -sSL -o wp.tar.xz "https://gitlab.freedesktop.org/wayland/wayland-protocols/-/releases/$WAYLAND_PROTOCOLS_VERSION/downloads/wayland-protocols-$WAYLAND_PROTOCOLS_VERSION.tar.xz"
tar xf wp.tar.xz
meson setup wp-build "wayland-protocols-$WAYLAND_PROTOCOLS_VERSION" --prefix=/usr -Dtests=false
sudo meson install -C wp-build
