#!/usr/bin/env bash

set -euo pipefail

PROJECT_ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." && pwd)"
CHUTNEY_DIR="$PROJECT_ROOT/benchmarks/chutney"

NETWORK_NAME="${CHUTNEY_NETWORK_NAME:-hs-v3-min}"

RUNTIME_DIR="$PROJECT_ROOT/.chutney"
DATA_DIR="$RUNTIME_DIR/net"

export CHUTNEY_DATA_DIR="$DATA_DIR"
export CHUTNEY_LISTEN_ADDRESS="${CHUTNEY_LISTEN_ADDRESS:-127.0.0.1}"
export CHUTNEY_DNS_CONF="${CHUTNEY_DNS_CONF:-/dev/null}"

die() {
    echo "[ERROR] $*" >&2
    exit 1
}

on_error() {
    local exit_code=$?

    echo
    echo "[ERROR] Chutney manager failed."
    echo "[ERROR] Command: $BASH_COMMAND"
    echo "[ERROR] Exit code: $exit_code"

    exit "$exit_code"
}

trap on_error ERR

check_dependencies() {
    [[ -x "$CHUTNEY_DIR/chutney" ]] || \
        die "Chutney executable not found: $CHUTNEY_DIR/chutney"

    [[ -f "$CHUTNEY_DIR/networks/$NETWORK_NAME" ]] || \
        die "Chutney network '$NETWORK_NAME' not found."

    command -v tor >/dev/null 2>&1 || \
        die "'tor' executable not found in PATH"

    command -v tor-gencert >/dev/null 2>&1 || \
        die "'tor-gencert' executable not found in PATH"
}

run_chutney() {
    (
        cd "$CHUTNEY_DIR"
        ./chutney "$@"
    )
}

network_initialized() {
    [[ -d "$DATA_DIR/nodes" ]]
}

print_ports() {
    echo
    echo "[i] Assigned Tor ports:"
    echo

    if [[ ! -d "$DATA_DIR/nodes" ]]; then
        echo "No Chutney nodes found."
        return
    fi

    local torrc
    local node

    while IFS= read -r torrc; do
        node="$(basename "$(dirname "$torrc")")"

        echo "----- $node -----"

        grep -E \
            '^[[:space:]]*(SocksPort|ControlPort|ORPort|DirPort)[[:space:]]+' \
            "$torrc" || true

        echo

    done < <(
        find -H "$DATA_DIR/nodes" \
            -mindepth 2 \
            -maxdepth 2 \
            -type f \
            -name torrc \
            | sort
    )
}

start_network() {
    check_dependencies

    echo "[*] Chutney directory:"
    echo "    $CHUTNEY_DIR"

    echo
    echo "[*] Network:"
    echo "    $NETWORK_NAME"

    echo
    echo "[*] Runtime data:"
    echo "    $DATA_DIR"

    echo
    echo "[*] Listen address:"
    echo "    $CHUTNEY_LISTEN_ADDRESS"

    echo

    if network_initialized; then
        echo "[*] Stopping previous network..."

        run_chutney stop 2>/dev/null || true

        echo "[*] Removing previous runtime data..."

        rm -rf "$DATA_DIR"
    fi

    mkdir -p "$RUNTIME_DIR"

    echo
    echo "[*] 1/4 Initializing '$NETWORK_NAME' network..."
    run_chutney init --net "$NETWORK_NAME"

    echo
    echo "[*] 2/4 Configuring Tor nodes..."
    run_chutney configure

    echo
    echo "[*] 2.5/4 Forcing CookieAuthentication 0..."
    for conf in "$DATA_DIR/nodes/"*/torrc; do
        echo "CookieAuthentication 0" >> "$conf"
    done

    sed -i 's/^SocksPort 0/SocksPort 127.0.0.1:9050/g' "$DATA_DIR/nodes/006r/torrc"

    echo
    echo "[*] 3/4 Starting Tor nodes..."
    run_chutney start

    echo "[*] Creating 32-byte fake cookie for Python orchestrator..."
    mkdir -p "$DATA_DIR/nodes/006r"
    dd if=/dev/zero of="$DATA_DIR/nodes/006r/control_auth_cookie" bs=32 count=1 2>/dev/null

    echo
    echo "[*] 4/4 Waiting for network bootstrap..."
    run_chutney wait_for_bootstrap

    echo
    echo "[+] Private Tor network bootstrapped successfully."

    echo
    echo "[i] Network status:"
    echo

    run_chutney status

    print_ports
}

stop_network() {
    check_dependencies

    if ! network_initialized; then
        echo "[i] Chutney network is not initialized."
        return
    fi

    echo "[*] Stopping private Tor network..."
    run_chutney stop
    echo "[+] Network stopped."
}

status_network() {
    check_dependencies

    if ! network_initialized; then
        echo "[i] Chutney network is not initialized."
        exit 1
    fi

    run_chutney status
}

verify_network() {
    check_dependencies

    if ! network_initialized; then
        die "Network is not initialized. Run '$0 start' first."
    fi

    echo "[*] Verifying private Tor network..."
    run_chutney verify

    echo
    echo "[+] Chutney verification successful."
}

clean_network() {
    check_dependencies

    if network_initialized; then
        echo "[*] Stopping Tor network..."
        run_chutney stop 2>/dev/null || true
    fi

    echo "[*] Removing runtime data..."
    rm -rf "$DATA_DIR"
    echo "[+] Environment cleaned."
}

ports_network() {
    print_ports
}

usage() {
    echo "Usage:"
    echo "  $0 start"
    echo "  $0 stop"
    echo "  $0 status"
    echo "  $0 verify"
    echo "  $0 ports"
    echo "  $0 clean"
}

case "${1:-}" in
    start)
        start_network
        ;;
    stop)
        stop_network
        ;;
    status)
        status_network
        ;;
    verify)
        verify_network
        ;;
    ports)
        ports_network
        ;;
    clean)
        clean_network
        ;;
    *)
        usage
        exit 1
        ;;
esac
