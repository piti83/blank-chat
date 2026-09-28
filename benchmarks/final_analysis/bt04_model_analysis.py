#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path

import pandas as pd


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("run_dir", type=Path)
    p.add_argument("--out", type=Path, required=True)
    return p.parse_args()


def clamped_exp_moments(lmbda: float, clamp_s: float) -> tuple[float, float, float]:
    e = math.exp(-lmbda * clamp_s)
    mean = clamp_s + e / lmbda
    second = clamp_s**2 + e * (2.0 * clamp_s / lmbda + 2.0 / (lmbda**2))
    variance = max(0.0, second - mean**2)
    std = math.sqrt(variance)
    cv = std / mean
    return mean, std, cv


def main() -> int:
    args = parse_args()
    run_dir = args.run_dir.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    source = pd.read_csv(run_dir / "summary.csv")
    cbr_run = json.loads((run_dir / "cbr/idle/run.json").read_text())
    poisson_run = json.loads((run_dir / "poisson/idle/run.json").read_text())

    cbr_mean = float(cbr_run["cbr_interval_ms"]) / 1000.0
    poisson_lambda = float(poisson_run["poisson_lambda"])
    clamp_s = float(poisson_run["poisson_min_clamp_ms"]) / 1000.0
    poisson_mean, poisson_std, poisson_cv = clamped_exp_moments(poisson_lambda, clamp_s)

    rows: list[dict[str, object]] = []
    for _, row in source.iterrows():
        mode = str(row["mode"])
        observed_mean = float(row["mean_iat_s"])
        observed_std = float(row["stddev_iat_s"])
        observed_rate = float(row["rate_per_second"])
        observed_cv = observed_std / observed_mean

        if mode == "cbr":
            expected_mean = cbr_mean
            expected_rate = 1.0 / cbr_mean
            expected_std = 0.0
            expected_cv = 0.0
        else:
            expected_mean = poisson_mean
            expected_rate = 1.0 / poisson_mean
            expected_std = poisson_std
            expected_cv = poisson_cv

        rows.append({
            "mode": mode,
            "profile": row["profile"],
            "transmissions": int(row["transmissions"]),
            "observed_rate_per_s": observed_rate,
            "expected_rate_per_s": expected_rate,
            "rate_error_percent": 100.0 * (observed_rate - expected_rate) / expected_rate,
            "observed_mean_iat_s": observed_mean,
            "expected_mean_iat_s": expected_mean,
            "mean_iat_error_percent": 100.0 * (observed_mean - expected_mean) / expected_mean,
            "observed_stddev_iat_s": observed_std,
            "expected_stddev_iat_s": expected_std,
            "observed_cv": observed_cv,
            "expected_cv": expected_cv,
            "cv_absolute_error": abs(observed_cv - expected_cv),
            "p95_iat_s": float(row["p95_iat_s"]),
        })

    result = pd.DataFrame(rows)
    result.to_csv(out / "bt04_model_validation.csv", index=False)

    metadata = {
        "cbr_interval_s": cbr_mean,
        "poisson_lambda_per_s": poisson_lambda,
        "poisson_min_clamp_s": clamp_s,
        "theoretical_clamped_poisson_mean_s": poisson_mean,
        "theoretical_clamped_poisson_std_s": poisson_std,
        "theoretical_clamped_poisson_cv": poisson_cv,
        "theoretical_clamped_poisson_rate_per_s": 1.0 / poisson_mean,
    }
    (out / "bt04_model_reference.json").write_text(json.dumps(metadata, indent=2) + "\n")

    print(result.to_string(index=False))
    print("\nReference model:")
    print(json.dumps(metadata, indent=2))
    print(f"[+] Wrote results to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
