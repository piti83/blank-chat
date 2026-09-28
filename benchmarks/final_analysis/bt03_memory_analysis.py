#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt
import pandas as pd


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("run_dir", type=Path)
    p.add_argument("--out", type=Path, required=True)
    return p.parse_args()


def main() -> int:
    args = parse_args()
    run_dir = args.run_dir.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    metrics = pd.read_csv(run_dir / "metrics.csv").sort_values("Timestamp").reset_index(drop=True)
    run = json.loads((run_dir / "run.json").read_text())
    loadgen = json.loads((run_dir / "loadgen_summary.json").read_text())

    threshold = float(run["memory_quota_percent"])
    metrics["elapsed_s"] = metrics["Timestamp"] - metrics["Timestamp"].iloc[0]
    metrics["VmRSS_MiB"] = metrics["VmRSS_kB"] / 1024.0

    crossed = metrics[metrics["RSS_percent"] >= threshold]
    first_cross = crossed.iloc[0] if not crossed.empty else None
    peak = metrics.loc[metrics["RSS_percent"].idxmax()]
    baseline = metrics.iloc[0]

    rejected = [p for p in loadgen.get("probes", []) if p.get("status") == "rejected"]

    summary = {
        "result": run.get("result"),
        "memory_quota_percent": threshold,
        "mem_total_mib": float(metrics["MemTotal_kB"].iloc[0] / 1024.0),
        "baseline_rss_mib": float(baseline["VmRSS_MiB"]),
        "baseline_rss_percent": float(baseline["RSS_percent"]),
        "quota_crossed": bool(not crossed.empty),
        "first_cross_elapsed_s": None if first_cross is None else float(first_cross["elapsed_s"]),
        "first_cross_rss_percent": None if first_cross is None else float(first_cross["RSS_percent"]),
        "first_cross_rss_mib": None if first_cross is None else float(first_cross["VmRSS_MiB"]),
        "active_connections_at_first_cross": None if first_cross is None else int(first_cross["ActiveConnections"]),
        "peak_elapsed_s": float(peak["elapsed_s"]),
        "peak_rss_percent": float(peak["RSS_percent"]),
        "peak_rss_mib": float(peak["VmRSS_MiB"]),
        "rss_growth_mib": float(peak["VmRSS_MiB"] - baseline["VmRSS_MiB"]),
        "max_active_connections": int(metrics["ActiveConnections"].max()),
        "vm_lck_peak_mib": float(metrics["VmLck_kB"].max() / 1024.0),
        "clients_requested": int(loadgen["clients_requested"]),
        "clients_connected": int(loadgen["clients_connected"]),
        "messages_sent": int(loadgen["messages_sent"]),
        "payload_mib_sent": float(loadgen["payload_bytes_sent"] / (1024 ** 2)),
        "wire_mib_sent": float(loadgen["wire_bytes_sent"] / (1024 ** 2)),
        "rejection_detected": bool(loadgen["rejection_detected"]),
        "first_rejected_probe_round": None if not rejected else int(rejected[0]["round_no"]),
        "liveness_ok": bool(loadgen["liveness_ok"]),
        "load_duration_s": float(loadgen["duration_seconds"]),
    }

    (out / "bt03_memory_summary.json").write_text(json.dumps(summary, indent=2) + "\n")
    pd.DataFrame([summary]).to_csv(out / "bt03_memory_summary.csv", index=False)

    fig, ax = plt.subplots(figsize=(10, 5.5))
    ax.plot(metrics["elapsed_s"], metrics["RSS_percent"], label="Server RSS")
    ax.axhline(threshold, linestyle="--", label=f"Quota threshold ({threshold:.0f}%)")
    if first_cross is not None:
        ax.axvline(float(first_cross["elapsed_s"]), linestyle=":", label="First threshold crossing")
    ax.set_xlabel("Elapsed time [s]")
    ax.set_ylabel("RSS / physical RAM [%]")
    ax.set_title("BT-03 — server resident memory under stress")
    ax.grid(True, alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out / "bt03_rss_over_time.png", dpi=180)
    plt.close(fig)

    print(json.dumps(summary, indent=2))
    print(f"[+] Wrote results to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
