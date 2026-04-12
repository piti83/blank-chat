#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

PRESET=${1:-linux-debug}
BUILD_DIR="build/$PRESET"

if [ ! -d "$BUILD_DIR" ]; then
    echo "Error: Build directory for: $PRESET not found."
    echo "Run: ./scripts/build.sh $PRESET first"
    exit 1
fi

echo "Running unit tests (Preset: $PRESET)..."
cd "$BUILD_DIR" || exit 1
ctest --output-on-failure
