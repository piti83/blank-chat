#!/usr/bin/env bash

PROJECT_ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"

alias build="python3 $PROJECT_ROOT/scripts/build.py"
alias run="python3 $PROJECT_ROOT/scripts/run.py"

alias bctest="python3 $PROJECT_ROOT/scripts/test.py"

alias coverage="python3 $PROJECT_ROOT/scripts/coverage.py"
alias format="python3 $PROJECT_ROOT/scripts/format.py"
alias clean-build="python3 $PROJECT_ROOT/scripts/clean.py"
alias valgrind-test="python3 $PROJECT_ROOT/scripts/valgrind.py"

alias build-yocto="python3 $PROJECT_ROOT/scripts/build_yocto.py"
alias get-sdk="python3 $PROJECT_ROOT/scripts/download_sdk.py"
alias push-sdk="python3 $PROJECT_ROOT/scripts/upload_sdk.py"

alias check-env="echo -e '\033[92m\033[1mEnvironment sourced\033[0m'"

echo -e "\033[92m\033[1mBlank Chat environment loaded successfully!\033[0m"
echo "You can now use the following commands:"
echo "  build [preset]       - Builds the project"
echo "  run [target]         - Runs the application"
echo "  bctest [preset]      - Runs unit tests"
echo "  coverage             - Generates the code coverage report"
echo "  format               - Formats C++ code"
echo "  clean-build          - Cleans the build directory"
echo "  valgrind-test        - Runs tests using Valgrind"
