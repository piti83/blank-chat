#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from dataclasses import dataclass, asdict
from pathlib import Path
from typing import Iterable


ALPHA = 0.05
POISSON_LAMBDA = 0.1
POISSON_MIN_CLAMP_S = 0.1


@dataclass
class ScenarioStats:
    mode: str
    profile: str
    transmissions: int
    iat_samples: int
    duration_seconds: float
    rate_per_second: float
    mean_iat_s: float | None
    median_iat_s: float | None
    stddev_iat_s: float | None
    min_iat_s: float | None
    max_iat_s: float | None
    p95_iat_s: float | None
    push_count: int
    poll_count: int
    ack_count: int


def percentile(values: list[float], p: float) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * p
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return ordered[lo]
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def read_transmissions(path: Path) -> list[dict[str, str]]:
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def iats_from_rows(rows: list[dict[str, str]]) -> list[float]:
    timestamps = [int(row["timestamp_ns"]) for row in rows]
    return [(b - a) / 1_000_000_000.0 for a, b in zip(timestamps, timestamps[1:])]


def scenario_stats(scenario_dir: Path) -> ScenarioStats:
    run = json.loads((scenario_dir / "run.json").read_text(encoding="utf-8"))
    rows = read_transmissions(scenario_dir / "transmissions.csv")
    iats = iats_from_rows(rows)

    actions = [row["frame_type"] for row in rows]
    duration = float(run["measurement_elapsed_seconds"])

    return ScenarioStats(
        mode=run["mode"],
        profile=run["profile"],
        transmissions=len(rows),
        iat_samples=len(iats),
        duration_seconds=duration,
        rate_per_second=(len(rows) / duration) if duration > 0 else 0.0,
        mean_iat_s=statistics.fmean(iats) if iats else None,
        median_iat_s=statistics.median(iats) if iats else None,
        stddev_iat_s=statistics.stdev(iats) if len(iats) >= 2 else None,
        min_iat_s=min(iats) if iats else None,
        max_iat_s=max(iats) if iats else None,
        p95_iat_s=percentile(iats, 0.95),
        push_count=actions.count("PUSH"),
        poll_count=actions.count("POLL"),
        ack_count=actions.count("ACK"),
    )


def ks_two_sample(x: list[float], y: list[float]) -> tuple[float, float]:
    if not x or not y:
        raise ValueError("KS test requires two non-empty samples")

    xs = sorted(x)
    ys = sorted(y)
    i = j = 0
    n = len(xs)
    m = len(ys)
    d = 0.0

    while i < n or j < m:
        if j >= m or (i < n and xs[i] <= ys[j]):
            value = xs[i]
        else:
            value = ys[j]

        while i < n and xs[i] <= value:
            i += 1
        while j < m and ys[j] <= value:
            j += 1

        d = max(d, abs(i / n - j / m))

    if d <= 0.0:
        return 0.0, 1.0

    n_eff = n * m / (n + m)
    root = math.sqrt(n_eff)
    lam = (root + 0.12 + 0.11 / root) * d

    p_value = 0.0
    for k in range(1, 200):
        term = 2.0 * ((-1) ** (k - 1)) * math.exp(-2.0 * k * k * lam * lam)
        p_value += term
        if abs(term) < 1e-12:
            break

    return d, max(0.0, min(1.0, p_value))


def empirical_cdf(values: Iterable[float]) -> tuple[list[float], list[float]]:
    ordered = sorted(values)
    if not ordered:
        return [], []
    n = len(ordered)
    return ordered, [(i + 1) / n for i in range(n)]


def poisson_model_cdf(x: float) -> float:
    if x < POISSON_MIN_CLAMP_S:
        return 0.0
    return 1.0 - math.exp(-POISSON_LAMBDA * x)


def model_ks_distance(values: list[float]) -> float | None:
    if not values:
        return None
    ordered = sorted(values)
    n = len(ordered)
    d = 0.0
    for i, value in enumerate(ordered, start=1):
        empirical_upper = i / n
        empirical_lower = (i - 1) / n
        theoretical = poisson_model_cdf(value)
        d = max(d, abs(empirical_upper - theoretical), abs(empirical_lower - theoretical))
    return d


def write_summary(path: Path, stats: list[ScenarioStats]) -> None:
    fields = list(asdict(stats[0]).keys()) if stats else []
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for item in stats:
            writer.writerow(asdict(item))


def write_ks(path: Path, rows: list[dict[str, object]]) -> None:
    fields = ["mode", "comparison", "n_idle", "n_chat", "D", "p_value", "alpha", "significant"]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def plot_results(root: Path, scenarios: dict[tuple[str, str], Path]) -> None:
    try:
        import matplotlib.pyplot as plt
    except ImportError as exc:
        raise RuntimeError(
            "matplotlib is required for BT-04 plots. On Arch install python-matplotlib."
        ) from exc

    def load(mode: str, profile: str) -> list[float]:
        return iats_from_rows(read_transmissions(scenarios[(mode, profile)] / "transmissions.csv"))

    for mode in ("cbr", "poisson"):
        for profile in ("idle", "chat", "intensive"):
            key = (mode, profile)
            if key not in scenarios:
                continue
            iats = load(mode, profile)
            if not iats:
                continue

            fig, ax = plt.subplots()
            ax.plot(range(1, len(iats) + 1), iats)
            ax.set_xlabel("Transmission interval index")
            ax.set_ylabel("IAT [s]")
            ax.set_title(f"BT-04 {mode.upper()} / {profile} — IAT over time")
            fig.tight_layout()
            fig.savefig(root / f"{mode}_{profile}_iat.png", dpi=160)
            plt.close(fig)

            fig, ax = plt.subplots()
            ax.hist(iats, bins=min(40, max(10, int(math.sqrt(len(iats))))))
            ax.set_xlabel("IAT [s]")
            ax.set_ylabel("Count")
            ax.set_title(f"BT-04 {mode.upper()} / {profile} — IAT histogram")
            fig.tight_layout()
            fig.savefig(root / f"{mode}_{profile}_hist.png", dpi=160)
            plt.close(fig)

    for mode in ("cbr", "poisson"):
        if (mode, "idle") not in scenarios or (mode, "chat") not in scenarios:
            continue
        idle = load(mode, "idle")
        chat = load(mode, "chat")
        x_idle, y_idle = empirical_cdf(idle)
        x_chat, y_chat = empirical_cdf(chat)

        fig, ax = plt.subplots()
        ax.step(x_idle, y_idle, where="post", label="Idle")
        ax.step(x_chat, y_chat, where="post", label="Chat")
        ax.set_xlabel("IAT [s]")
        ax.set_ylabel("ECDF")
        ax.set_title(f"BT-04 {mode.upper()} — Idle vs Chat")
        ax.legend()
        fig.tight_layout()
        fig.savefig(root / f"{mode}_idle_vs_chat_ecdf.png", dpi=160)
        plt.close(fig)

    if ("cbr", "idle") in scenarios and ("poisson", "idle") in scenarios:
        cbr = load("cbr", "idle")
        poisson = load("poisson", "idle")
        x_cbr, y_cbr = empirical_cdf(cbr)
        x_poisson, y_poisson = empirical_cdf(poisson)

        fig, ax = plt.subplots()
        ax.step(x_cbr, y_cbr, where="post", label="CBR Idle")
        ax.step(x_poisson, y_poisson, where="post", label="Poisson Idle")
        ax.set_xlabel("IAT [s]")
        ax.set_ylabel("ECDF")
        ax.set_title("BT-04 — CBR vs Poisson Idle IAT")
        ax.legend()
        fig.tight_layout()
        fig.savefig(root / "cbr_vs_poisson_iat.png", dpi=160)
        plt.close(fig)

    if ("poisson", "idle") in scenarios:
        values = load("poisson", "idle")
        x_emp, y_emp = empirical_cdf(values)
        x_model = sorted(set([POISSON_MIN_CLAMP_S, *x_emp]))
        y_model = [poisson_model_cdf(x) for x in x_model]

        fig, ax = plt.subplots()
        ax.step(x_emp, y_emp, where="post", label="Empirical")
        ax.plot(x_model, y_model, label="Clamped exponential model")
        ax.set_xlabel("IAT [s]")
        ax.set_ylabel("CDF")
        ax.set_title("BT-04 Poisson Idle — empirical vs configured model")
        ax.legend()
        fig.tight_layout()
        fig.savefig(root / "poisson_idle_model_ecdf.png", dpi=160)
        plt.close(fig)


def discover_scenarios(root: Path) -> dict[tuple[str, str], Path]:
    scenarios: dict[tuple[str, str], Path] = {}
    for mode in ("cbr", "poisson"):
        for profile in ("idle", "chat", "intensive"):
            path = root / mode / profile
            if (path / "run.json").exists() and (path / "transmissions.csv").exists():
                scenarios[(mode, profile)] = path
    return scenarios


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="Analyze BT-04 traffic-timing results")
    parser.add_argument("result_root", type=Path)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    root = args.result_root.resolve()
    scenarios = discover_scenarios(root)
    if not scenarios:
        raise SystemExit(f"No BT-04 scenario results found under {root}")

    stats = [scenario_stats(path) for _, path in sorted(scenarios.items())]
    write_summary(root / "summary.csv", stats)

    ks_rows: list[dict[str, object]] = []
    for mode in ("cbr", "poisson"):
        if (mode, "idle") not in scenarios or (mode, "chat") not in scenarios:
            continue
        idle = iats_from_rows(read_transmissions(scenarios[(mode, "idle")] / "transmissions.csv"))
        chat = iats_from_rows(read_transmissions(scenarios[(mode, "chat")] / "transmissions.csv"))
        d, p_value = ks_two_sample(idle, chat)
        ks_rows.append(
            {
                "mode": mode,
                "comparison": "idle_vs_chat",
                "n_idle": len(idle),
                "n_chat": len(chat),
                "D": d,
                "p_value": p_value,
                "alpha": ALPHA,
                "significant": p_value < ALPHA,
            }
        )
    write_ks(root / "ks_results.csv", ks_rows)

    poisson_model_d = None
    if ("poisson", "idle") in scenarios:
        poisson_idle = iats_from_rows(
            read_transmissions(scenarios[("poisson", "idle")] / "transmissions.csv")
        )
        poisson_model_d = model_ks_distance(poisson_idle)

    analysis = {
        "alpha": ALPHA,
        "ks_method": "two-sample asymptotic KS approximation",
        "poisson_lambda": POISSON_LAMBDA,
        "poisson_min_clamp_seconds": POISSON_MIN_CLAMP_S,
        "poisson_idle_descriptive_D_to_clamped_model": poisson_model_d,
        "note": (
            "Idle-vs-Chat p-values are inferential results and are not used as a functional "
            "PASS/FAIL gate. The Poisson one-sample model comparison is descriptive because "
            "the 100 ms clamp introduces a point mass."
        ),
    }
    (root / "analysis.json").write_text(json.dumps(analysis, indent=2) + "\n", encoding="utf-8")

    plot_results(root, scenarios)

    print(f"[+] Wrote {root / 'summary.csv'}")
    print(f"[+] Wrote {root / 'ks_results.csv'}")
    print(f"[+] Wrote plots under {root}")

    for item in stats:
        print(
            f"[+] {item.mode}/{item.profile}: tx={item.transmissions}, "
            f"mean IAT={item.mean_iat_s}, rate={item.rate_per_second:.5f}/s"
        )

    for row in ks_rows:
        interpretation = (
            "statistically significant difference detected"
            if row["significant"]
            else "no statistically significant difference detected"
        )
        print(
            f"[i] KS {row['mode']} idle vs chat: D={row['D']:.6f}, "
            f"p={row['p_value']:.6g}, alpha={ALPHA} -> {interpretation}"
        )

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
