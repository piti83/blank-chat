#!/usr/bin/env python3
from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent

DEFAULT_RUNS = {
    "BT-03": "20260925_001238",
    "BT-04": "20260928_002000",
    "BT-05": "20260925_064621",
    "BT-06": "20260925_233653",
}


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("--results-root", type=Path, default=PROJECT_ROOT / "benchmarks/results")
    p.add_argument("--out", type=Path, default=PROJECT_ROOT / "benchmarks/results/final-analysis")
    p.add_argument("--bootstrap", type=int, default=10_000)
    return p.parse_args()


def run(cmd: list[str]) -> None:
    print("[i] $ " + " ".join(cmd), flush=True)
    subprocess.run(cmd, check=True, cwd=PROJECT_ROOT)


def main() -> int:
    args = parse_args()
    results = args.results_root.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    jobs = [
        ("BT-03", "bt03_memory_analysis.py", []),
        ("BT-04", "bt04_model_analysis.py", []),
        ("BT-05", "bt05_volumetric_analysis.py", []),
        ("BT-06", "bt06_statistical_analysis.py", ["--bootstrap", str(args.bootstrap)]),
    ]

    for test_id, script, extra in jobs:
        run_dir = results / test_id / DEFAULT_RUNS[test_id]
        if not run_dir.is_dir():
            raise SystemExit(f"Missing final run directory: {run_dir}")
        run([
            sys.executable,
            str(HERE / script),
            str(run_dir),
            "--out",
            str(out / test_id),
            *extra,
        ])

    print(f"\n[+] Final analysis complete: {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
