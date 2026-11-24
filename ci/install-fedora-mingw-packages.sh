#!/usr/bin/env bash
# Installs what the windows-mingw presets need to cross-build the Windows zip, in a fedora:44 container, as root.
set -euo pipefail

dnf install -y --setopt=install_weak_deps=False cmake ninja-build git mingw64-gcc-c++ mingw64-qt6-qtbase \
  mingw64-qt6-qtsvg mingw64-qt6-qtmultimedia mingw64-libjpeg-turbo zip
