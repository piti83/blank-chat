#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

PRESET=${1:-linux-debug}
BUILD_DIR="build/$PRESET"

echo "Configuring CMake (Preset: $PRESET)..."
cmake --preset "$PRESET"

if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed."
    exit 1
fi

echo "Building the project (Preset: $PRESET)..."
cmake --build "$BUILD_DIR"

if [ $? -eq 0 ]; then
    echo "Build completed successfully!"
else
    echo "Error: Build failed."
    exit 1
fi
