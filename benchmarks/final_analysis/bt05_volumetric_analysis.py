#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

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

    df = pd.read_csv(run_dir / "volumetric_results.csv").set_index("mode")
    cbr = df.loc["cbr"]
    poisson = df.loc["poisson"]

    v_diff = int(cbr["V_total"] - poisson["V_total"])
    summary = {
        "messages_per_mode": int(cbr["messages"]),
        "payload_size_bytes": int(cbr["payload_size"]),
        "V_user_bytes": int(cbr["V_user"]),
        "cbr_V_total_bytes": int(cbr["V_total"]),
        "poisson_V_total_bytes": int(poisson["V_total"]),
        "absolute_V_total_difference_bytes_cbr_minus_poisson": v_diff,
        "poisson_V_total_reduction_vs_cbr_percent": 100.0 * v_diff / float(cbr["V_total"]),
        "cbr_overhead_percent": float(cbr["overhead_percent"]),
        "poisson_overhead_percent": float(poisson["overhead_percent"]),
        "overhead_difference_percentage_points": abs(float(cbr["overhead_percent"] - poisson["overhead_percent"])),
        "cbr_expansion_factor": float(cbr["expansion_factor"]),
        "poisson_expansion_factor": float(poisson["expansion_factor"]),
        "expansion_factor_absolute_difference": abs(float(cbr["expansion_factor"] - poisson["expansion_factor"])),
        "poisson_expansion_reduction_vs_cbr_percent": 100.0 * float(cbr["expansion_factor"] - poisson["expansion_factor"]) / float(cbr["expansion_factor"]),
        "cbr_server_edge_bytes": int(cbr["server_edge_bytes"]),
        "poisson_server_edge_bytes": int(poisson["server_edge_bytes"]),
        "cbr_captured_packets": int(cbr["captured_packets"]),
        "poisson_captured_packets": int(poisson["captured_packets"]),
    }

    (out / "bt05_volumetric_derived.json").write_text(json.dumps(summary, indent=2) + "\n")
    pd.DataFrame([summary]).to_csv(out / "bt05_volumetric_derived.csv", index=False)

    print(json.dumps(summary, indent=2))
    print(f"[+] Wrote results to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
