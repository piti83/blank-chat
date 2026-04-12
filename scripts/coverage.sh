#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

PRESET="linux-coverage"
BUILD_DIR="build/$PRESET"

echo "Setting up environment for code coverage (Preset: $PRESET)..."

cmake --preset "$PRESET"
if [ $? -ne 0 ]; then
    echo "Error: CMake configuration failed for preset $PRESET."
    exit 1
fi

echo "Building the project..."
cmake --build "$BUILD_DIR"
if [ $? -ne 0 ]; then
    echo "Error: Build failed."
    exit 1
fi

echo "Running tests and generating coverage report..."
cmake --build "$BUILD_DIR" --target coverage

if [ $? -eq 0 ]; then
    echo "Coverage report generated successfully!"
    echo "Open this file in your browser: $BUILD_DIR/coverage_report/index.html"
else
    echo "Error: Failed to generate coverage report."
    exit 1
fi
