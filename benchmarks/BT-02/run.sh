#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
TEST_DIR="$PROJECT_ROOT/benchmarks/BT-02"

if [[ "${1:-}" == "--prepare" ]]; then
    shift

    echo "[i] Applying BT-02 benchmark-only diagnostic events..."
    python3 "$TEST_DIR/apply_diagnostics.py"

    echo "[i] Rebuilding the Yocto client image..."
    python3 "$PROJECT_ROOT/scripts/build_yocto.py" --target client --clean
fi

exec python3 "$TEST_DIR/orchestrator.py" "$@"
