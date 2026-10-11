#!/bin/bash
#
#  build.sh - configure and build Rackbox Plus in Release, then verify it.
#
#  "It builds" and "it measures correctly" are the same command here: a
#  multi-effect whose crossfade steps or whose empty chain is not a wire
#  compiles perfectly well.
#
set -euo pipefail

PROJECT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BUILD_DIR="$PROJECT_DIR/build"

cd "$PROJECT_DIR"

cmake -B "$BUILD_DIR" -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build "$BUILD_DIR"

echo
echo "Measuring the engine:"
echo
"$BUILD_DIR/RackboxPlusCheck_artefacts/Release/RackboxPlusCheck"

echo
echo "Checking the processor and rendering the panel:"
echo
"$BUILD_DIR/EditorShot_artefacts/Release/EditorShot" "$BUILD_DIR/shots"

echo
echo "Built:"
ls -d "$BUILD_DIR/RackboxPlus_artefacts/Release/"*/*.{vst3,component,app} 2>/dev/null || true
echo "Panel: $BUILD_DIR/shots/rackbox-plus.png (and rackbox-plus-mod/-reverb/-delay/-dist/-dark.png)"
