#!/usr/bin/env python3
from __future__ import annotations

import csv
import hashlib
import json
import sys
from pathlib import Path
from typing import Any

SCRIPT_PATH = Path(__file__).resolve()
PROJECT_ROOT = SCRIPT_PATH.parents[2]
RESULTS_ROOT = PROJECT_ROOT / "benchmarks" / "results"
FINAL_ROOT = RESULTS_ROOT / "final-analysis"
ANALYSIS_SCRIPTS_ROOT = PROJECT_ROOT / "benchmarks" / "final_analysis"

SELECTED_RUNS = {
    "BT-01": "20260925_000512",
    "BT-02": "20260925_000936",
    "BT-03": "20260925_001238",
    "BT-04": "20260928_002000",
    "BT-05": "20260925_064621",
    "BT-06": "20260925_233653",
}


class FinalSummaryError(RuntimeError):
    pass


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise FinalSummaryError(f"Missing required file: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def load_csv_rows(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise FinalSummaryError(f"Missing required file: {path}")
    with path.open("r", encoding="utf-8", newline="") as handle:
        return list(csv.DictReader(handle))


def require_dir(path: Path) -> None:
    if not path.is_dir():
        raise FinalSummaryError(f"Missing required directory: {path}")


def require_passed(run_dir: Path, *candidates: str) -> str:
    for name in candidates:
        path = run_dir / name
        if not path.is_file():
            continue
        data = load_json(path)
        result = data.get("result") or data.get("suite_result")
        if result != "passed":
            raise FinalSummaryError(f"{path}: expected result='passed', got {result!r}")
        return str(result)
    raise FinalSummaryError(
        f"{run_dir}: none of status files exist: {', '.join(candidates)}"
    )


def number(value: str | int | float | None) -> float | None:
    if value is None or value == "":
        return None
    return float(value)


def sha256_file(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for chunk in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def find_row(rows: list[dict[str, str]], **where: str) -> dict[str, str]:
    for row in rows:
        if all(row.get(key) == value for key, value in where.items()):
            return row
    raise FinalSummaryError(f"No CSV row matching {where}")


def main() -> int:
    FINAL_ROOT.mkdir(parents=True, exist_ok=True)

    run_dirs = {test: RESULTS_ROOT / test / stamp for test, stamp in SELECTED_RUNS.items()}
    for path in run_dirs.values():
        require_dir(path)

    statuses = {
        "BT-01": require_passed(run_dirs["BT-01"], "run.json"),
        "BT-02": require_passed(run_dirs["BT-02"], "run.json"),
        "BT-03": require_passed(run_dirs["BT-03"], "run.json"),
        "BT-04": require_passed(run_dirs["BT-04"], "matrix_run.json"),
        "BT-05": require_passed(run_dirs["BT-05"], "suite_run.json", "run.json"),
        "BT-06": require_passed(run_dirs["BT-06"], "suite_run.json", "run.json"),
    }

    bt03 = load_json(FINAL_ROOT / "BT-03" / "bt03_memory_summary.json")
    bt04_rows = load_csv_rows(FINAL_ROOT / "BT-04" / "bt04_model_validation.csv")
    bt04_ref = load_json(FINAL_ROOT / "BT-04" / "bt04_model_reference.json")
    bt05 = load_json(FINAL_ROOT / "BT-05" / "bt05_volumetric_derived.json")
    bt06 = load_json(FINAL_ROOT / "BT-06" / "bt06_inference.json")
    bt06_ci = load_csv_rows(FINAL_ROOT / "BT-06" / "bt06_descriptive_ci.csv")

    bt04_compact = []
    for mode, profile in [
        ("cbr", "idle"), ("cbr", "chat"), ("cbr", "intensive"),
        ("poisson", "idle"), ("poisson", "chat"), ("poisson", "intensive"),
    ]:
        row = find_row(bt04_rows, mode=mode, profile=profile)
        bt04_compact.append({
            "mode": mode,
            "profile": profile,
            "transmissions": int(row["transmissions"]),
            "observed_rate_per_s": number(row["observed_rate_per_s"]),
            "rate_error_percent": number(row["rate_error_percent"]),
            "observed_mean_iat_s": number(row["observed_mean_iat_s"]),
            "expected_mean_iat_s": number(row["expected_mean_iat_s"]),
            "mean_iat_error_percent": number(row["mean_iat_error_percent"]),
            "observed_cv": number(row["observed_cv"]),
            "expected_cv": number(row["expected_cv"]),
            "p95_iat_s": number(row["p95_iat_s"]),
        })

    def ci(mode: str, metric: str) -> dict[str, float]:
        row = find_row(bt06_ci, mode=mode, metric=metric)
        return {
            "estimate_ms": float(row["estimate_ms"]),
            "ci95_low_ms": float(row["ci95_low_ms"]),
            "ci95_high_ms": float(row["ci95_high_ms"]),
        }

    summary: dict[str, Any] = {
        "schema_version": 1,
        "selected_runs": SELECTED_RUNS,
        "all_functional_results_passed": True,
        "BT-01": {"result": statuses["BT-01"], "run": SELECTED_RUNS["BT-01"]},
        "BT-02": {"result": statuses["BT-02"], "run": SELECTED_RUNS["BT-02"]},
        "BT-03": {
            "result": statuses["BT-03"], "run": SELECTED_RUNS["BT-03"],
            "memory_quota_percent": bt03["memory_quota_percent"],
            "mem_total_mib": bt03["mem_total_mib"],
            "baseline_rss_mib": bt03["baseline_rss_mib"],
            "first_cross_elapsed_s": bt03["first_cross_elapsed_s"],
            "first_cross_rss_percent": bt03["first_cross_rss_percent"],
            "first_cross_rss_mib": bt03["first_cross_rss_mib"],
            "peak_rss_percent": bt03["peak_rss_percent"],
            "peak_rss_mib": bt03["peak_rss_mib"],
            "vm_lck_peak_mib": bt03["vm_lck_peak_mib"],
            "clients_connected": bt03["clients_connected"],
            "messages_sent": bt03["messages_sent"],
            "payload_mib_sent": bt03["payload_mib_sent"],
            "rejection_detected": bt03["rejection_detected"],
            "first_rejected_probe_round": bt03["first_rejected_probe_round"],
            "liveness_ok": bt03["liveness_ok"],
        },
        "BT-04": {
            "result": statuses["BT-04"], "run": SELECTED_RUNS["BT-04"],
            "reference_model": bt04_ref, "scenarios": bt04_compact,
        },
        "BT-05": {
            "result": statuses["BT-05"], "run": SELECTED_RUNS["BT-05"],
            "messages_per_mode": bt05["messages_per_mode"],
            "payload_size_bytes": bt05["payload_size_bytes"],
            "V_user_bytes": bt05["V_user_bytes"],
            "cbr_V_total_bytes": bt05["cbr_V_total_bytes"],
            "poisson_V_total_bytes": bt05["poisson_V_total_bytes"],
            "absolute_V_total_difference_bytes_cbr_minus_poisson": bt05["absolute_V_total_difference_bytes_cbr_minus_poisson"],
            "poisson_V_total_reduction_vs_cbr_percent": bt05["poisson_V_total_reduction_vs_cbr_percent"],
            "cbr_overhead_percent": bt05["cbr_overhead_percent"],
            "poisson_overhead_percent": bt05["poisson_overhead_percent"],
            "overhead_difference_percentage_points": bt05["overhead_difference_percentage_points"],
            "cbr_expansion_factor": bt05["cbr_expansion_factor"],
            "poisson_expansion_factor": bt05["poisson_expansion_factor"],
            "expansion_factor_absolute_difference": bt05["expansion_factor_absolute_difference"],
        },
        "BT-06": {
            "result": statuses["BT-06"], "run": SELECTED_RUNS["BT-06"],
            "n_cbr": bt06["n_cbr"], "n_poisson": bt06["n_poisson"],
            "latency_ci95": {
                "cbr": {"mean": ci("cbr", "mean_ms"), "median": ci("cbr", "median_ms"), "p95": ci("cbr", "p95_ms")},
                "poisson": {"mean": ci("poisson", "mean_ms"), "median": ci("poisson", "median_ms"), "p95": ci("poisson", "p95_ms")},
            },
            "mann_whitney_u": bt06["mann_whitney_u"],
            "mann_whitney_p_two_sided": bt06["mann_whitney_p_two_sided"],
            "ks_D": bt06["ks_D"], "ks_p_two_sided": bt06["ks_p_two_sided"],
            "cliffs_delta_cbr_minus_poisson": bt06["cliffs_delta_cbr_minus_poisson"],
            "probability_cbr_greater_than_poisson_with_half_ties": bt06["probability_cbr_greater_than_poisson_with_half_ties"],
            "mean_latency_difference_poisson_minus_cbr_ms": bt06["mean_latency_difference_poisson_minus_cbr_ms"],
            "mean_latency_ratio_poisson_over_cbr": bt06["mean_latency_ratio_poisson_over_cbr"],
            "median_latency_difference_poisson_minus_cbr_ms": bt06["median_latency_difference_poisson_minus_cbr_ms"],
            "p95_latency_difference_poisson_minus_cbr_ms": bt06["p95_latency_difference_poisson_minus_cbr_ms"],
            "cbr_queue_to_tx_share_percent": bt06["cbr_queue_to_tx_share_percent"],
            "cbr_tx_to_rx_share_percent": bt06["cbr_tx_to_rx_share_percent"],
            "poisson_queue_to_tx_share_percent": bt06["poisson_queue_to_tx_share_percent"],
            "poisson_tx_to_rx_share_percent": bt06["poisson_tx_to_rx_share_percent"],
            "max_estimated_clock_uncertainty_ms": bt06["max_estimated_clock_uncertainty_ms"],
            "mean_difference_to_max_clock_uncertainty_ratio": bt06["mean_difference_to_max_clock_uncertainty_ratio"],
            "bootstrap_seed": bt06["bootstrap_seed"], "bootstrap_resamples": bt06["bootstrap_resamples"],
        },
    }

    summary_json = FINAL_ROOT / "FINAL_SUMMARY.json"
    summary_json.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")

    flat_rows: list[tuple[str, str, str, str]] = []
    def add(test: str, metric: str, value: Any, unit: str = "") -> None:
        flat_rows.append((test, metric, str(value), unit))

    add("BT-01", "result", statuses["BT-01"])
    add("BT-02", "result", statuses["BT-02"])
    add("BT-03", "memory_quota", bt03["memory_quota_percent"], "%")
    add("BT-03", "first_cross_elapsed", bt03["first_cross_elapsed_s"], "s")
    add("BT-03", "first_cross_rss", bt03["first_cross_rss_percent"], "%")
    add("BT-03", "peak_rss", bt03["peak_rss_percent"], "%")
    add("BT-03", "messages_sent", bt03["messages_sent"], "messages")
    add("BT-03", "rejection_detected", bt03["rejection_detected"])
    add("BT-03", "liveness_ok", bt03["liveness_ok"])

    for row in bt04_compact:
        prefix = f'{row["mode"]}_{row["profile"]}'
        add("BT-04", f"{prefix}_transmissions", row["transmissions"], "frames")
        add("BT-04", f"{prefix}_mean_iat", row["observed_mean_iat_s"], "s")
        add("BT-04", f"{prefix}_cv", row["observed_cv"])

    add("BT-05", "cbr_V_total", bt05["cbr_V_total_bytes"], "B")
    add("BT-05", "poisson_V_total", bt05["poisson_V_total_bytes"], "B")
    add("BT-05", "poisson_reduction_vs_cbr", bt05["poisson_V_total_reduction_vs_cbr_percent"], "%")
    add("BT-05", "cbr_overhead", bt05["cbr_overhead_percent"], "%")
    add("BT-05", "poisson_overhead", bt05["poisson_overhead_percent"], "%")
    add("BT-05", "cbr_expansion", bt05["cbr_expansion_factor"], "x")
    add("BT-05", "poisson_expansion", bt05["poisson_expansion_factor"], "x")

    for mode in ("cbr", "poisson"):
        for metric in ("mean", "median", "p95"):
            entry = summary["BT-06"]["latency_ci95"][mode][metric]
            add("BT-06", f"{mode}_{metric}_latency", entry["estimate_ms"], "ms")
            add("BT-06", f"{mode}_{metric}_ci95_low", entry["ci95_low_ms"], "ms")
            add("BT-06", f"{mode}_{metric}_ci95_high", entry["ci95_high_ms"], "ms")

    add("BT-06", "mean_latency_ratio_poisson_over_cbr", bt06["mean_latency_ratio_poisson_over_cbr"], "x")
    add("BT-06", "cliffs_delta_cbr_minus_poisson", bt06["cliffs_delta_cbr_minus_poisson"])
    add("BT-06", "mann_whitney_p_two_sided", bt06["mann_whitney_p_two_sided"])
    add("BT-06", "ks_D", bt06["ks_D"])
    add("BT-06", "ks_p_two_sided", bt06["ks_p_two_sided"])
    add("BT-06", "clock_uncertainty_max", bt06["max_estimated_clock_uncertainty_ms"], "ms")

    summary_csv = FINAL_ROOT / "FINAL_SUMMARY.csv"
    with summary_csv.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["test", "metric", "value", "unit"])
        writer.writerows(flat_rows)

    selection_path = FINAL_ROOT / "SELECTED_RUNS.json"
    selection_path.write_text(json.dumps(SELECTED_RUNS, indent=2) + "\n", encoding="utf-8")

    hash_targets: list[Path] = []
    for run_dir in run_dirs.values():
        hash_targets.extend(path for path in run_dir.rglob("*") if path.is_file())
    hash_targets.extend(
        path for path in FINAL_ROOT.rglob("*")
        if path.is_file() and path.name not in {"SHA256SUMS", "FREEZE_MANIFEST.json"}
    )
    hash_targets.extend(
        path for path in ANALYSIS_SCRIPTS_ROOT.rglob("*")
        if path.is_file() and "__pycache__" not in path.parts
    )

    unique_targets = sorted(set(hash_targets), key=lambda p: str(p.relative_to(PROJECT_ROOT)))
    sums_path = FINAL_ROOT / "SHA256SUMS"
    with sums_path.open("w", encoding="utf-8") as handle:
        for path in unique_targets:
            handle.write(f"{sha256_file(path)}  {path.relative_to(PROJECT_ROOT)}\n")

    manifest = {
        "schema_version": 1,
        "selected_runs": SELECTED_RUNS,
        "summary_json": str(summary_json.relative_to(PROJECT_ROOT)),
        "summary_csv": str(summary_csv.relative_to(PROJECT_ROOT)),
        "sha256sums": str(sums_path.relative_to(PROJECT_ROOT)),
        "analysis_scripts_dir": str(ANALYSIS_SCRIPTS_ROOT.relative_to(PROJECT_ROOT)),
        "hashed_file_count": len(unique_targets),
        "all_functional_results_passed": True,
    }
    manifest_path = FINAL_ROOT / "FREEZE_MANIFEST.json"
    manifest_path.write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")

    print("[+] Final benchmark summary generated")
    print(f"[+] {summary_json}")
    print(f"[+] {summary_csv}")
    print(f"[+] {selection_path}")
    print(f"[+] {sums_path} ({len(unique_targets)} files)")
    print(f"[+] {manifest_path}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except FinalSummaryError as exc:
        print(f"[!] {exc}", file=sys.stderr)
        raise SystemExit(1)
