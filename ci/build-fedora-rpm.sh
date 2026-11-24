#!/usr/bin/env bash
# Builds the release with Fedora's own Qt and packs it as an RPM for this Fedora release, in build-linux-release/.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset linux-release -DDOODLEBOLD_RPM_RELEASE="1.fc$(rpm -E %fedora)"
cmake --build --preset linux-release
cpack --preset fedora-rpm
