#!/bin/bash

cd "$(dirname "$0")/.." || exit 1

TARGET=${1:-server}
PRESET=${2:-linux-debug}
EXECUTABLE=""

if [ "$TARGET" == "server" ]; then
    EXECUTABLE="build/$PRESET/apps/blank_chat_server"
elif [ "$TARGET" == "client" ]; then
    EXECUTABLE="build/$PRESET/apps/blank_chat_client"
else
    echo "Target unknown: $TARGET"
    echo "Usage: $0 [server|client] [preset]"
    exit 1
fi

if [ -f "$EXECUTABLE" ]; then
    echo "Running $TARGET..."
    "$EXECUTABLE"
else
    echo "Error: executable not found: $EXECUTABLE."
    echo "Build the project using: ./scripts/build.sh $PRESET"
    exit 1
fi
