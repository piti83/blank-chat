#!/usr/bin/env bash
set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/../.." && pwd)"
exec python3 "$PROJECT_ROOT/benchmarks/BT-01/orchestrator.py" "$@"

