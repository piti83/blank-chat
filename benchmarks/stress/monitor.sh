#!/bin/sh
set -u

OUTPUT="${1:-/tmp/stress_metrics.csv}"
SERVER_PID="${2:-}"

if [ -z "$SERVER_PID" ]; then
    SERVER_PID="$(pidof blank_chat_server 2>/dev/null | awk '{print $1}')"
fi

if [ -z "$SERVER_PID" ] || ! kill -0 "$SERVER_PID" 2>/dev/null; then
    echo "[monitor] blank_chat_server PID not found or not running" >&2
    exit 1
fi

MEM_TOTAL_KB="$(awk '/^MemTotal:/ {print $2; exit}' /proc/meminfo)"
if [ -z "$MEM_TOTAL_KB" ] || [ "$MEM_TOTAL_KB" -le 0 ]; then
    echo "[monitor] Could not read MemTotal from /proc/meminfo" >&2
    exit 1
fi

echo "Timestamp,VmRSS_kB,VmLck_kB,MemTotal_kB,RSS_percent,ActiveConnections" > "$OUTPUT"

while kill -0 "$SERVER_PID" 2>/dev/null; do
    STATUS="/proc/$SERVER_PID/status"

    if [ ! -r "$STATUS" ]; then
        break
    fi

    TS="$(date +%s)"
    RSS_KB="$(awk '/^VmRSS:/ {print $2; exit}' "$STATUS")"
    LCK_KB="$(awk '/^VmLck:/ {print $2; exit}' "$STATUS")"

    RSS_KB="${RSS_KB:-0}"
    LCK_KB="${LCK_KB:-0}"

    RSS_PERCENT="$(awk -v rss="$RSS_KB" -v total="$MEM_TOTAL_KB" \
        'BEGIN { if (total > 0) printf "%.4f", (rss * 100.0) / total; else printf "0.0000" }')"

    ACTIVE_CONNECTIONS="$(
        awk '
            $4 == "01" {
                split($2, local_addr, ":")
                if (toupper(local_addr[2]) == "1F90") {
                    count++
                }
            }
            END { print count + 0 }
        ' /proc/net/tcp /proc/net/tcp6 2>/dev/null
    )"

    echo "$TS,$RSS_KB,$LCK_KB,$MEM_TOTAL_KB,$RSS_PERCENT,$ACTIVE_CONNECTIONS" >> "$OUTPUT"
    sleep 1
done
