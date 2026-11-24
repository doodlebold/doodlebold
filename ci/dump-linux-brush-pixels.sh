#!/usr/bin/env bash
# Renders the brush-pixel cases whose names contain the given text and dumps Linux's pixels, for another platform to compare with.
# Usage: ci/dump-linux-brush-pixels.sh <part of a case name, such as grid/pencil-> <output folder>
set -euo pipefail
cd "$(dirname "$0")/.."

only=$1
output=$2

cmake --preset linux-develop
cmake --build --preset linux-develop --target doodlebold_brush_pixels
build/tests/doodlebold_brush_pixels --check tests/fixtures/brush-pixels --only "$only" --dump "$output"
