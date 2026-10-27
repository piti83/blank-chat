#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path

import matplotlib.pyplot as plt


MODES = ("cbr", "poisson")


class AnalysisError(RuntimeError):
    pass


def load_summary(root: Path, mode: str) -> dict:
    path = root / mode / "volumetric_summary.json"
    if not path.exists():
        raise AnalysisError(f"Missing {path}")
    data = json.loads(path.read_text(encoding="utf-8"))

    required = {
        "mode",
        "messages",
        "payload_size",
        "V_user",
        "V_total",
        "overhead_percent",
        "expansion_factor",
        "duration_seconds",
        "messages_received",
        "acks_delivered",
        "captured_packets",
        "tor_client_node",
        "tor_or_event_count",
        "tor_or_read_bytes",
        "tor_or_written_bytes",
        "server_edge_bytes",
    }
    missing = sorted(required - data.keys())
    if missing:
        raise AnalysisError(f"{path} is missing fields: {', '.join(missing)}")
    return data


def write_csv(root: Path, rows: list[dict]) -> Path:
    path = root / "volumetric_results.csv"
    fields = [
        "mode",
        "messages",
        "payload_size",
        "V_user",
        "V_total",
        "overhead_percent",
        "expansion_factor",
        "duration_seconds",
        "messages_received",
        "acks_delivered",
        "captured_packets",
        "tor_client_node",
        "tor_or_event_count",
        "tor_or_read_bytes",
        "tor_or_written_bytes",
        "server_edge_bytes",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in rows:
            writer.writerow({key: row[key] for key in fields})
    return path


def write_comparison(root: Path, cbr: dict, poisson: dict) -> Path:
    delta_abs = abs(float(cbr["overhead_percent"]) - float(poisson["overhead_percent"]))
    comparison = {
        "N_CBR_percent": cbr["overhead_percent"],
        "N_Poisson_percent": poisson["overhead_percent"],
        "absolute_difference_percent_points": delta_abs,
        "V_total_CBR": cbr["V_total"],
        "V_total_Poisson": poisson["V_total"],
        "E_CBR": cbr["expansion_factor"],
        "E_Poisson": poisson["expansion_factor"],
        "note": (
            "The absolute difference is |N_CBR - N_Poisson|. "
            "No expected winner or target overhead is used as a test criterion."
        ),
    }
    path = root / "volumetric_comparison.json"
    path.write_text(json.dumps(comparison, indent=2) + "\n", encoding="utf-8")
    return path


def write_plot(root: Path, rows: list[dict]) -> Path:
    labels = [row["mode"].upper() for row in rows]
    totals_mib = [float(row["V_total"]) / (1024.0 * 1024.0) for row in rows]

    fig, ax = plt.subplots(figsize=(7, 5))
    bars = ax.bar(labels, totals_mib)
    ax.set_ylabel("Tor OR transport volume [MiB]")
    ax.set_title("BT-05 Tor transport overhead")

    for bar, row in zip(bars, rows):
        ax.text(
            bar.get_x() + bar.get_width() / 2.0,
            bar.get_height(),
            f"N={float(row['overhead_percent']):.2f}%\nE={float(row['expansion_factor']):.2f}x",
            ha="center",
            va="bottom",
        )

    fig.tight_layout()
    path = root / "volumetric_comparison.png"
    fig.savefig(path, dpi=160)
    plt.close(fig)
    return path


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze BT-05 volumetric benchmark results")
    parser.add_argument("result_root", type=Path)
    args = parser.parse_args()

    root = args.result_root.resolve()
    rows = [load_summary(root, mode) for mode in MODES]

    cbr, poisson = rows
    if cbr["messages"] != poisson["messages"]:
        raise AnalysisError("CBR and Poisson used different message counts")
    if cbr["payload_size"] != poisson["payload_size"]:
        raise AnalysisError("CBR and Poisson used different payload sizes")
    if cbr["V_user"] != poisson["V_user"]:
        raise AnalysisError("CBR and Poisson used different V_user")

    csv_path = write_csv(root, rows)
    comparison_path = write_comparison(root, cbr, poisson)
    plot_path = write_plot(root, rows)

    print(f"[+] Wrote {csv_path}")
    print(f"[+] Wrote {comparison_path}")
    print(f"[+] Wrote {plot_path}")

    for row in rows:
        print(
            f"[+] {row['mode']}: messages={row['messages']}, "
            f"V_user={row['V_user']} B, Tor_OR_V_total={row['V_total']} B, "
            f"N={float(row['overhead_percent']):.4f}%, "
            f"E={float(row['expansion_factor']):.4f}x, "
            f"duration={float(row['duration_seconds']) / 3600.0:.3f} h"
        )

    delta = abs(float(cbr["overhead_percent"]) - float(poisson["overhead_percent"]))
    print(f"[i] |N_CBR - N_Poisson| = {delta:.4f} percentage points")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] BT-05 analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
