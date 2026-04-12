#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

echo "Cleaning project..."

if [ -d "build" ]; then
    rm -rf build/
    echo "build/ directory removed."
else
    echo "build/ directory does not exist. No cleaning required."
fi
