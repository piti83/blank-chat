#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

PRESET=${1:-linux-debug}

echo "Configuring (Preset: $PRESET)..."
cmake --preset "$PRESET"

if [ $? -ne 0 ]; then
    echo "CMake configuration error!"
    exit 1
fi

echo "Building project (Preset: $PRESET)..."
cmake --build --preset "$PRESET"

if [ $? -eq 0 ]; then
    echo "Building finished successfully"
else
    echo "Error occured during building process!"
    exit 1
fi
