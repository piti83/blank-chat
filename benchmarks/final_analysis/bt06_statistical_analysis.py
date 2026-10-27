#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path

import matplotlib.pyplot as plt
import numpy as np
import pandas as pd
from scipy import stats

BOOTSTRAP_SEED = 260928
BOOTSTRAP_RESAMPLES = 10_000


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser()
    p.add_argument("run_dir", type=Path)
    p.add_argument("--out", type=Path, required=True)
    p.add_argument("--bootstrap", type=int, default=BOOTSTRAP_RESAMPLES)
    return p.parse_args()


def percentile_linear(values: np.ndarray, q: float, axis=None):
    return np.percentile(values, q * 100.0, axis=axis, method="linear")


def bootstrap_stats(values: np.ndarray, n_resamples: int, rng: np.random.Generator) -> dict[str, tuple[float, float, float]]:
    n = len(values)
    means = np.empty(n_resamples)
    medians = np.empty(n_resamples)
    p95s = np.empty(n_resamples)

    batch = 500
    done = 0
    while done < n_resamples:
        size = min(batch, n_resamples - done)
        samples = rng.choice(values, size=(size, n), replace=True)
        sl = slice(done, done + size)
        means[sl] = samples.mean(axis=1)
        medians[sl] = np.median(samples, axis=1)
        p95s[sl] = percentile_linear(samples, 0.95, axis=1)
        done += size

    def ci(arr: np.ndarray, estimate: float) -> tuple[float, float, float]:
        lo, hi = np.percentile(arr, [2.5, 97.5], method="linear")
        return estimate, float(lo), float(hi)

    return {
        "mean_ms": ci(means, float(values.mean())),
        "median_ms": ci(medians, float(np.median(values))),
        "p95_ms": ci(p95s, float(percentile_linear(values, 0.95))),
    }


def main() -> int:
    args = parse_args()
    run_dir = args.run_dir.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=True)

    cbr_df = pd.read_csv(run_dir / "latency_cbr.csv")
    poi_df = pd.read_csv(run_dir / "latency_poisson.csv")
    cbr = cbr_df["latency_ms"].to_numpy(dtype=float)
    poi = poi_df["latency_ms"].to_numpy(dtype=float)

    rng = np.random.default_rng(BOOTSTRAP_SEED)
    cbr_boot = bootstrap_stats(cbr, args.bootstrap, rng)
    poi_boot = bootstrap_stats(poi, args.bootstrap, rng)

    descriptive_rows: list[dict[str, object]] = []
    for mode, boot in (("cbr", cbr_boot), ("poisson", poi_boot)):
        for metric, (estimate, low, high) in boot.items():
            descriptive_rows.append({
                "mode": mode,
                "metric": metric,
                "estimate_ms": estimate,
                "ci95_low_ms": low,
                "ci95_high_ms": high,
                "bootstrap_resamples": args.bootstrap,
                "bootstrap_seed": BOOTSTRAP_SEED,
            })
    pd.DataFrame(descriptive_rows).to_csv(out / "bt06_descriptive_ci.csv", index=False)

    mw = stats.mannwhitneyu(cbr, poi, alternative="two-sided", method="asymptotic")
    ks = stats.ks_2samp(cbr, poi, alternative="two-sided", method="auto")
    cliffs_delta = 2.0 * float(mw.statistic) / (len(cbr) * len(poi)) - 1.0

    cbr_mean = float(cbr.mean())
    poi_mean = float(poi.mean())
    mean_diff = poi_mean - cbr_mean

    cbr_qtx = float(cbr_df["queue_to_tx_ms"].mean())
    cbr_txrx = float(cbr_df["tx_to_rx_ms"].mean())
    poi_qtx = float(poi_df["queue_to_tx_ms"].mean())
    poi_txrx = float(poi_df["tx_to_rx_ms"].mean())

    max_clock_uncertainty = max(
        float(cbr_df["estimated_clock_uncertainty_ms"].max()),
        float(poi_df["estimated_clock_uncertainty_ms"].max()),
    )

    inference = {
        "n_cbr": int(len(cbr)),
        "n_poisson": int(len(poi)),
        "mann_whitney_u": float(mw.statistic),
        "mann_whitney_p_two_sided": float(mw.pvalue),
        "ks_D": float(ks.statistic),
        "ks_p_two_sided": float(ks.pvalue),
        "cliffs_delta_cbr_minus_poisson": cliffs_delta,
        "probability_cbr_greater_than_poisson_with_half_ties": float(mw.statistic / (len(cbr) * len(poi))),
        "mean_latency_difference_poisson_minus_cbr_ms": mean_diff,
        "mean_latency_ratio_poisson_over_cbr": poi_mean / cbr_mean,
        "median_latency_difference_poisson_minus_cbr_ms": float(np.median(poi) - np.median(cbr)),
        "p95_latency_difference_poisson_minus_cbr_ms": float(percentile_linear(poi, 0.95) - percentile_linear(cbr, 0.95)),
        "cbr_queue_to_tx_share_percent": 100.0 * cbr_qtx / cbr_mean,
        "cbr_tx_to_rx_share_percent": 100.0 * cbr_txrx / cbr_mean,
        "poisson_queue_to_tx_share_percent": 100.0 * poi_qtx / poi_mean,
        "poisson_tx_to_rx_share_percent": 100.0 * poi_txrx / poi_mean,
        "max_estimated_clock_uncertainty_ms": max_clock_uncertainty,
        "mean_difference_to_max_clock_uncertainty_ratio": mean_diff / max_clock_uncertainty,
        "bootstrap_seed": BOOTSTRAP_SEED,
        "bootstrap_resamples": int(args.bootstrap),
    }
    (out / "bt06_inference.json").write_text(json.dumps(inference, indent=2) + "\n")
    pd.DataFrame([inference]).to_csv(out / "bt06_inference.csv", index=False)

    fig, ax = plt.subplots(figsize=(8, 5.5))
    labels = ["CBR", "Poisson"]
    queue_means = [cbr_qtx, poi_qtx]
    delivery_means = [cbr_txrx, poi_txrx]
    x = np.arange(len(labels))
    width = 0.34
    ax.bar(x - width / 2, queue_means, width, label="QUEUE→TX")
    ax.bar(x + width / 2, delivery_means, width, label="TX→RX")
    ax.set_xticks(x, labels)
    ax.set_ylabel("Mean latency [ms]")
    ax.set_title("BT-06 — mean latency decomposition")
    ax.grid(True, axis="y", alpha=0.25)
    ax.legend()
    fig.tight_layout()
    fig.savefig(out / "bt06_latency_decomposition.png", dpi=180)
    plt.close(fig)

    print("Bootstrap 95% CIs:")
    print(pd.DataFrame(descriptive_rows).to_string(index=False))
    print("\nInferential / effect-size summary:")
    print(json.dumps(inference, indent=2))
    print(f"[+] Wrote results to {out}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
