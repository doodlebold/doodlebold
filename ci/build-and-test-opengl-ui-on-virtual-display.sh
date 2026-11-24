#!/usr/bin/env bash
# Builds the linux-ui-gl preset and runs the UI tests that need OpenGL, which skip on the offscreen platform, on a virtual X
# display with Mesa's software renderer.
set -euo pipefail
cd "$(dirname "$0")/.."

cmake --preset linux-ui-gl
cmake --build --preset linux-ui-gl
xvfb-run -a -s "-screen 0 1920x1080x24" ctest --preset linux-ui-gl
