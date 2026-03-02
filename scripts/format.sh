#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

PRESET=${1:-linux-debug}

if [ ! -d "build/$PRESET" ]; then
    echo "Configuring format target..."
    cmake --preset "$PRESET"
fi

echo "Formatting..."
cmake --build build/"$PRESET" --target format
echo "Format done."
