#!/usr/bin/env bash
set -Eeuo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$PROJECT_ROOT"

BT04_REPETITIONS=1
SKIP_INITIAL_BUILD=0

usage() {
    cat <<'EOF'
Usage:
  ./benchmarks/run_all_tests.sh [options]

Options:
  --bt04-repetitions N   Number of complete BT-04 matrix runs (default: 1)
  --skip-initial-build   Reuse existing clean server/client Yocto images
  -h, --help             Show this help

The script runs:
  BT-01 -> BT-02 -> BT-03 -> BT-04 -> BT-05 -> BT-06

BT-06 reuses the BT-05 data set and does not generate network traffic again.
TA-01 and TA-02 are manual acceptance tests and are intentionally not run here.
EOF
}

while [[ $# -gt 0 ]]; do
    case "$1" in
        --bt04-repetitions)
            [[ $# -ge 2 ]] || { echo "[!] Missing value for --bt04-repetitions" >&2; exit 2; }
            BT04_REPETITIONS="$2"
            shift 2
            ;;
        --skip-initial-build)
            SKIP_INITIAL_BUILD=1
            shift
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "[!] Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if ! [[ "$BT04_REPETITIONS" =~ ^[1-9][0-9]*$ ]]; then
    echo "[!] --bt04-repetitions must be a positive integer" >&2
    exit 2
fi

if [[ "$(git branch --show-current)" != "benchmarks" ]]; then
    echo "[!] Final benchmark suite must be run from branch 'benchmarks'." >&2
    exit 1
fi

if ! git ls-files --error-unmatch benchmarks/run_all_tests.sh >/dev/null 2>&1; then
    echo "[!] benchmarks/run_all_tests.sh is not tracked. Commit the final benchmark tooling before running the suite." >&2
    exit 1
fi

if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
    echo "[!] Tracked worktree is dirty. Commit/stash/restore changes before the final suite." >&2
    git status --short
    exit 1
fi

STAMP="$(date +%Y%m%d_%H%M%S)"
SUITE_DIR="$PROJECT_ROOT/benchmarks/results/RUN-ALL/$STAMP"
SUITE_LOG="$SUITE_DIR/run_all.log"
MANIFEST="$SUITE_DIR/manifest.txt"
mkdir -p "$SUITE_DIR"

COMMIT="$(git rev-parse HEAD)"

log() {
    printf '%s\n' "$*" | tee -a "$SUITE_LOG"
}

run_logged() {
    log
    log "[i] $*"
    "$@" 2>&1 | tee -a "$SUITE_LOG"
}

restore_benchmark_sources() {
    git restore -- \
        libs/cli/src/repl.cpp \
        libs/network/src/tcp_client.cpp
}

cleanup() {
    local rc=$?
    set +e
    restore_benchmark_sources >/dev/null 2>&1
    {
        echo
        echo "finished_at=$(date --iso-8601=seconds)"
        echo "exit_code=$rc"
    } >> "$MANIFEST"
    if [[ $rc -ne 0 ]]; then
        echo "[!] RUN-ALL stopped with exit code $rc. Earlier test results were preserved." | tee -a "$SUITE_LOG"
    fi
    trap - EXIT INT TERM
    exit "$rc"
}
trap cleanup EXIT INT TERM

latest_result_dir() {
    local test_id="$1"
    local root="$PROJECT_ROOT/benchmarks/results/$test_id"
    [[ -d "$root" ]] || return 1
    find "$root" -mindepth 1 -maxdepth 1 -type d -printf '%T@ %p\n' \
        | sort -nr \
        | head -n1 \
        | cut -d' ' -f2-
}

record_latest() {
    local test_id="$1"
    local result_dir
    result_dir="$(latest_result_dir "$test_id")"
    echo "${test_id}_result=${result_dir}" >> "$MANIFEST"
}

prepare_client_diagnostics() {
    local test_id="$1"
    local patcher="$2"

    restore_benchmark_sources

    log
    log "[i] Preparing $test_id diagnostic client image..."
    run_logged python3 "$patcher"
    run_logged python3 scripts/build_yocto.py --target client --clean

    # The WIC contains deterministic instrumentation produced by the committed
    # patcher. Restore tracked sources before the measurement so run metadata
    # points at a clean commit rather than an incidental patched worktree.
    restore_benchmark_sources

    if [[ -n "$(git status --porcelain --untracked-files=no)" ]]; then
        echo "[!] Source restoration after $test_id preparation failed." >&2
        git status --short >&2
        exit 1
    fi
}

{
    echo "started_at=$(date --iso-8601=seconds)"
    echo "commit=$COMMIT"
    echo "branch=benchmarks"
    echo "bt04_repetitions=$BT04_REPETITIONS"
    echo "skip_initial_build=$SKIP_INITIAL_BUILD"
    echo "bt02_patcher_sha256=$(sha256sum benchmarks/BT-02/apply_diagnostics.py | awk '{print $1}')"
    echo "bt04_patcher_sha256=$(sha256sum benchmarks/BT-04/apply_diagnostics.py | awk '{print $1}')"
    echo "bt05_patcher_sha256=$(sha256sum benchmarks/BT-05/apply_diagnostics.py | awk '{print $1}')"
} > "$MANIFEST"

log "[i] Blank Chat final benchmark suite"
log "[i] Commit: $COMMIT"
log "[i] Suite artifacts: $SUITE_DIR"

restore_benchmark_sources

if [[ "$SKIP_INITIAL_BUILD" -eq 0 ]]; then
    log
    log "[i] Building clean server and client Yocto images from the suite commit..."
    run_logged python3 scripts/build_yocto.py --target both --clean
else
    log "[i] Skipping initial Yocto build by request."
fi

log
log "========== BT-01 =========="
run_logged ./benchmarks/BT-01/run.sh
record_latest "BT-01"

log
log "========== BT-02 =========="
prepare_client_diagnostics "BT-02" "benchmarks/BT-02/apply_diagnostics.py"
run_logged ./benchmarks/BT-02/run.sh
record_latest "BT-02"

log
log "========== BT-03 =========="
# BT-03 uses the server plus its host-side load generator; the client VM image
# left from BT-02 is not part of the measured workload.
run_logged ./benchmarks/BT-03/run.sh
record_latest "BT-03"

log
log "========== BT-04 =========="
prepare_client_diagnostics "BT-04" "benchmarks/BT-04/apply_diagnostics.py"
for ((i = 1; i <= BT04_REPETITIONS; ++i)); do
    log
    log "[i] BT-04 full matrix repetition $i/$BT04_REPETITIONS"
    run_logged ./benchmarks/BT-04/run.sh
    result_dir="$(latest_result_dir "BT-04")"
    echo "BT-04_run_${i}=${result_dir}" >> "$MANIFEST"
done

log
log "========== BT-05 =========="
prepare_client_diagnostics "BT-05/BT-06" "benchmarks/BT-05/apply_diagnostics.py"
run_logged ./benchmarks/BT-05/run.sh
BT05_RUN="$(latest_result_dir "BT-05")"
echo "BT-05_result=${BT05_RUN}" >> "$MANIFEST"

log
log "========== BT-06 =========="
run_logged ./benchmarks/BT-06/run.sh --bt05-run "$BT05_RUN"
record_latest "BT-06"

restore_benchmark_sources

log
log "========== SUITE COMPLETE =========="
log "[+] BT-01 through BT-06 completed successfully."
log "[i] BT-06 reused: $BT05_RUN"
log "[i] Manifest: $MANIFEST"
