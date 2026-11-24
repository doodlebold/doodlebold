#!/usr/bin/env bash
# Installs what the Linux presets need to build and test Doodlebold with GCC and Clang, in a fedora:44 container, as root.
set -euo pipefail

dnf install -y --setopt=install_weak_deps=False cmake ninja-build gcc-c++ clang git python3 libasan libubsan \
  qt6-qtbase-devel qt6-qtsvg-devel qt6-qtwayland-devel mesa-libGL-devel mesa-dri-drivers gtest-devel gmock-devel \
  libjpeg-turbo-devel wayland-devel wayland-protocols-devel xorg-x11-server-Xvfb xvfb-run rpm-build
