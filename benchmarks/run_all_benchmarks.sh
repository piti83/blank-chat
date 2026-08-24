#!/bin/bash

set -e

echo "======================================================"
echo "   Blank Chat - Automated Batch Testing Suite"
echo "======================================================"

echo "[*] Please enter your password to authorize sudo for the test duration:"
sudo -v

while true; do
    sudo -n true
    sleep 300
    kill -0 "$$" || exit
done 2>/dev/null &
SUDO_KEEPALIVE_PID=$!

trap 'kill $SUDO_KEEPALIVE_PID 2>/dev/null' EXIT

shopt -s expand_aliases
source bc-env.sh

DURATION=60
PROFILES=("chat" "intensive" "idle")
MODES=("cbr" "poisson")

for PROFILE in "${PROFILES[@]}"; do
    for MODE in "${MODES[@]}"; do
        echo ""
        echo "======================================================"
        echo "STARTING TEST: Profile = ${PROFILE^^}, Mode = ${MODE^^}"
        echo "======================================================"

        echo "[*] Stopping and cleaning Chutney network..."
        chutney-stop || true
        chutney-clean
        chutney-start
        sleep 5

        echo "[*] Recreating clean virtual machines..."
        python3 benchmarks/vm_manager.py

        echo "[*] Waiting 180 seconds for guest operating systems to boot..."
        sleep 180

        echo "[*] Starting the 60-minute simulation..."
        python3 benchmarks/orchestrator.py --profile "$PROFILE" --mode "$MODE" --duration $DURATION

        echo "[*] Test ${PROFILE^^} + ${MODE^^} completed successfully!"
        echo "======================================================"
        sleep 10
    done
done

echo ""
echo "ALL TESTS COMPLETED SUCCESSFULLY! YOU CAN NOW BEGIN LOG ANALYSIS."
