#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import shutil
import statistics
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import matplotlib.pyplot as plt


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BENCHMARKS_DIR = PROJECT_ROOT / "benchmarks"
DEFAULT_RESULTS_DIR = BENCHMARKS_DIR / "results" / "BT-06"
MODES = ("cbr", "poisson")


class Bt06Error(RuntimeError):
    pass


@dataclass(frozen=True)
class Anchor:
    host_mid_ns: int
    vm_ns: int
    offset_vm_minus_host_ns: int
    rtt_ns: int


@dataclass(frozen=True)
class CorrectedTime:
    host_ns: int
    offset_ns: float
    uncertainty_ns: float


def percentile(values: list[float], q: float) -> float:
    if not values:
        raise Bt06Error("Cannot calculate percentile of an empty sample")
    if not 0.0 <= q <= 1.0:
        raise Bt06Error("Percentile q must be in [0, 1]")
    ordered = sorted(values)
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return ordered[lo]
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


def read_json(path: Path) -> dict:
    if not path.exists():
        raise Bt06Error(f"Missing required file: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise Bt06Error(f"Invalid JSON in {path}: {exc}") from exc


def anchor_from(calibration: dict, phase: str, client: str) -> Anchor:
    try:
        selected = calibration[phase][client]["selected"]
        return Anchor(
            host_mid_ns=int(selected["host_mid_ns"]),
            vm_ns=int(selected["vm_ns"]),
            offset_vm_minus_host_ns=int(selected["offset_vm_minus_host_ns"]),
            rtt_ns=int(selected["rtt_ns"]),
        )
    except (KeyError, TypeError, ValueError) as exc:
        raise Bt06Error(
            f"clock_calibration.json is missing a valid {phase}/{client}/selected sample"
        ) from exc


def correct_vm_time(vm_event_ns: int, pre: Anchor, post: Anchor) -> CorrectedTime:
    if post.vm_ns <= pre.vm_ns:
        raise Bt06Error("Post-run clock calibration is not later than pre-run calibration")

    alpha = (vm_event_ns - pre.vm_ns) / (post.vm_ns - pre.vm_ns)
    # Events should be inside the two calibration points. Clamp tiny boundary
    # excursions caused by logging/order, but reject a clearly wrong dataset.
    if alpha < -0.05 or alpha > 1.05:
        raise Bt06Error(
            "Message timestamp lies materially outside the pre/post clock calibration window"
        )
    alpha = min(1.0, max(0.0, alpha))

    offset = (
        pre.offset_vm_minus_host_ns
        + alpha * (post.offset_vm_minus_host_ns - pre.offset_vm_minus_host_ns)
    )
    rtt = pre.rtt_ns + alpha * (post.rtt_ns - pre.rtt_ns)
    host_ns = int(round(vm_event_ns - offset))

    # Midpoint clock estimation has an approximate one-way timing uncertainty
    # bounded by half the observed command round trip, assuming no stronger
    # synchronization guarantee.
    return CorrectedTime(
        host_ns=host_ns,
        offset_ns=offset,
        uncertainty_ns=max(0.0, rtt / 2.0),
    )


def index_events(entries: list[dict], label: str) -> dict[str, int]:
    indexed: dict[str, int] = {}
    for entry in entries:
        try:
            msg_id = str(entry["msg_id"]).lower()
            timestamp_ns = int(entry["timestamp_ns"])
        except (KeyError, TypeError, ValueError) as exc:
            raise Bt06Error(f"Malformed {label} event: {entry!r}") from exc
        if msg_id in indexed:
            raise Bt06Error(f"Duplicate {label} event for message {msg_id}")
        indexed[msg_id] = timestamp_ns
    return indexed


def build_latency_rows(bt05_root: Path, mode: str) -> tuple[list[dict], dict]:
    mode_dir = bt05_root / mode
    events = read_json(mode_dir / "message_events.json")
    calibration = read_json(mode_dir / "clock_calibration.json")
    run_meta = read_json(mode_dir / "run.json")

    queued = index_events(events.get("queue", []), "QUEUE")
    received = index_events(events.get("rx", []), "RX")

    expected = int(run_meta.get("messages_requested", 0))
    if expected <= 0:
        raise Bt06Error(f"Invalid messages_requested in {mode_dir / 'run.json'}")
    if len(queued) != expected or len(received) != expected:
        raise Bt06Error(
            f"{mode}: expected {expected} QUEUE and RX events, got "
            f"QUEUE={len(queued)}, RX={len(received)}"
        )
    if set(queued) != set(received):
        raise Bt06Error(f"{mode}: QUEUE and RX message-ID sets differ")

    a_pre = anchor_from(calibration, "pre", "client_a")
    a_post = anchor_from(calibration, "post", "client_a")
    b_pre = anchor_from(calibration, "pre", "client_b")
    b_post = anchor_from(calibration, "post", "client_b")

    rows: list[dict] = []
    for msg_id, start_vm_ns in sorted(queued.items(), key=lambda item: item[1]):
        end_vm_ns = received[msg_id]
        start = correct_vm_time(start_vm_ns, a_pre, a_post)
        end = correct_vm_time(end_vm_ns, b_pre, b_post)
        latency_ns = end.host_ns - start.host_ns
        if latency_ns < 0:
            raise Bt06Error(
                f"{mode}: negative corrected latency for {msg_id}: {latency_ns / 1e6:.3f} ms"
            )

        rows.append(
            {
                "message_id": msg_id,
                "t_start_vm_ns": start_vm_ns,
                "t_end_vm_ns": end_vm_ns,
                "t_start_host_corrected_ns": start.host_ns,
                "t_end_host_corrected_ns": end.host_ns,
                "latency_ms": latency_ns / 1_000_000.0,
                "sender_offset_ms": start.offset_ns / 1_000_000.0,
                "receiver_offset_ms": end.offset_ns / 1_000_000.0,
                "estimated_clock_uncertainty_ms": (
                    start.uncertainty_ns + end.uncertainty_ns
                )
                / 1_000_000.0,
            }
        )

    metadata = {
        "mode": mode,
        "expected_messages": expected,
        "source_run": run_meta,
        "clock": {
            "client_a_pre_offset_ms": a_pre.offset_vm_minus_host_ns / 1e6,
            "client_a_post_offset_ms": a_post.offset_vm_minus_host_ns / 1e6,
            "client_a_offset_drift_ms": (
                a_post.offset_vm_minus_host_ns - a_pre.offset_vm_minus_host_ns
            )
            / 1e6,
            "client_b_pre_offset_ms": b_pre.offset_vm_minus_host_ns / 1e6,
            "client_b_post_offset_ms": b_post.offset_vm_minus_host_ns / 1e6,
            "client_b_offset_drift_ms": (
                b_post.offset_vm_minus_host_ns - b_pre.offset_vm_minus_host_ns
            )
            / 1e6,
            "client_a_pre_rtt_ms": a_pre.rtt_ns / 1e6,
            "client_a_post_rtt_ms": a_post.rtt_ns / 1e6,
            "client_b_pre_rtt_ms": b_pre.rtt_ns / 1e6,
            "client_b_post_rtt_ms": b_post.rtt_ns / 1e6,
        },
    }
    return rows, metadata


def summarize(mode: str, rows: list[dict], metadata: dict) -> dict:
    values = [float(row["latency_ms"]) for row in rows]
    if not values:
        raise Bt06Error(f"{mode}: no latency samples")

    stddev = statistics.stdev(values) if len(values) > 1 else 0.0
    uncertainty_values = [float(row["estimated_clock_uncertainty_ms"]) for row in rows]
    clock = metadata["clock"]

    return {
        "mode": mode,
        "n": len(values),
        "mean_ms": statistics.fmean(values),
        "median_ms": statistics.median(values),
        "p95_ms": percentile(values, 0.95),
        "stddev_ms": stddev,
        "min_ms": min(values),
        "max_ms": max(values),
        "mean_estimated_clock_uncertainty_ms": statistics.fmean(uncertainty_values),
        "max_estimated_clock_uncertainty_ms": max(uncertainty_values),
        **clock,
    }


def write_latency_csv(path: Path, rows: list[dict]) -> None:
    fields = [
        "message_id",
        "t_start_vm_ns",
        "t_end_vm_ns",
        "t_start_host_corrected_ns",
        "t_end_host_corrected_ns",
        "latency_ms",
        "sender_offset_ms",
        "receiver_offset_ms",
        "estimated_clock_uncertainty_ms",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_summary_csv(path: Path, summaries: list[dict]) -> None:
    fields = [
        "mode",
        "n",
        "mean_ms",
        "median_ms",
        "p95_ms",
        "stddev_ms",
        "min_ms",
        "max_ms",
        "mean_estimated_clock_uncertainty_ms",
        "max_estimated_clock_uncertainty_ms",
        "client_a_pre_offset_ms",
        "client_a_post_offset_ms",
        "client_a_offset_drift_ms",
        "client_b_pre_offset_ms",
        "client_b_post_offset_ms",
        "client_b_offset_drift_ms",
        "client_a_pre_rtt_ms",
        "client_a_post_rtt_ms",
        "client_b_pre_rtt_ms",
        "client_b_post_rtt_ms",
    ]
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        for row in summaries:
            writer.writerow({field: row[field] for field in fields})


def write_histogram(path: Path, mode_rows: dict[str, list[dict]]) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in MODES:
        values = [float(row["latency_ms"]) for row in mode_rows[mode]]
        ax.hist(values, bins=40, alpha=0.5, label=mode.upper())
    ax.set_xlabel("End-to-end latency [ms]")
    ax.set_ylabel("Messages")
    ax.set_title("BT-06 latency distribution")
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def write_ecdf(path: Path, mode_rows: dict[str, list[dict]]) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in MODES:
        values = sorted(float(row["latency_ms"]) for row in mode_rows[mode])
        n = len(values)
        y = [(i + 1) / n for i in range(n)]
        ax.plot(values, y, label=mode.upper())
    ax.set_xlabel("End-to-end latency [ms]")
    ax.set_ylabel("ECDF")
    ax.set_title("BT-06 latency ECDF")
    ax.set_ylim(0.0, 1.0)
    ax.legend()
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def write_boxplot(path: Path, mode_rows: dict[str, list[dict]]) -> None:
    fig, ax = plt.subplots(figsize=(7, 5))
    data = [
        [float(row["latency_ms"]) for row in mode_rows["cbr"]],
        [float(row["latency_ms"]) for row in mode_rows["poisson"]],
    ]
    ax.boxplot(data, tick_labels=["CBR", "Poisson"], showfliers=True)
    ax.set_ylabel("End-to-end latency [ms]")
    ax.set_title("BT-06 latency comparison")
    fig.tight_layout()
    fig.savefig(path, dpi=160)
    plt.close(fig)


def copy_source_logs(bt05_root: Path, output: Path) -> None:
    for mode in MODES:
        for source_name in ("client_a.log", "client_b.log", "clock_calibration.json"):
            src = bt05_root / mode / source_name
            if src.exists():
                shutil.copy2(src, output / f"{mode}_{source_name}")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="BT-06 end-to-end latency analysis using a calibrated BT-05 message series"
    )
    parser.add_argument(
        "--bt05-run",
        required=True,
        type=Path,
        help="Path to one completed BT-05 result root containing cbr/ and poisson/",
    )
    parser.add_argument(
        "--output",
        type=Path,
        help="Optional BT-06 output directory; defaults to benchmarks/results/BT-06/<BT05-run-name>",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    bt05_root = args.bt05_run.resolve()
    if not bt05_root.exists():
        raise Bt06Error(f"BT-05 result directory does not exist: {bt05_root}")

    output = (
        args.output.resolve()
        if args.output
        else (DEFAULT_RESULTS_DIR / bt05_root.name).resolve()
    )
    output.mkdir(parents=True, exist_ok=True)

    suite = read_json(bt05_root / "suite_run.json")
    if suite.get("result") != "passed":
        raise Bt06Error("BT-05 source run did not finish with result=passed")
    if set(suite.get("completed", [])) != {"cbr", "poisson"}:
        raise Bt06Error("BT-05 source run must contain completed CBR and Poisson modes")

    mode_rows: dict[str, list[dict]] = {}
    mode_metadata: dict[str, dict] = {}
    summaries: list[dict] = []

    for mode in MODES:
        rows, metadata = build_latency_rows(bt05_root, mode)
        mode_rows[mode] = rows
        mode_metadata[mode] = metadata
        summary = summarize(mode, rows, metadata)
        summaries.append(summary)
        write_latency_csv(output / f"latency_{mode}.csv", rows)

        max_rtt = max(
            summary["client_a_pre_rtt_ms"],
            summary["client_a_post_rtt_ms"],
            summary["client_b_pre_rtt_ms"],
            summary["client_b_post_rtt_ms"],
        )
        if max_rtt > 100.0:
            print(
                f"[!] {mode}: clock-calibration RTT reached {max_rtt:.3f} ms; "
                "interpret sub-RTT latency differences cautiously",
                file=sys.stderr,
            )

    write_summary_csv(output / "latency_summary.csv", summaries)
    write_histogram(output / "latency_histogram.png", mode_rows)
    write_ecdf(output / "latency_ecdf.png", mode_rows)
    write_boxplot(output / "latency_boxplot.png", mode_rows)
    copy_source_logs(bt05_root, output)

    result_meta = {
        "created_at": datetime.now().astimezone().isoformat(),
        "source_bt05_run": str(bt05_root),
        "source_commit": suite.get("commit"),
        "source_dirty_worktree": suite.get("dirty_worktree"),
        "messages": suite.get("messages"),
        "message_size": suite.get("message_size"),
        "cbr_interval_ms": suite.get("cbr_interval_ms"),
        "poisson_lambda": suite.get("poisson_lambda"),
        "latency_definition": (
            "QUEUE timestamp on sender -> RX timestamp after receiver decrypt/process/cache; "
            "VM system_clock values corrected to the host clock using pre/post minimum-RTT "
            "midpoint calibration with linear offset interpolation"
        ),
        "clock_calibration": mode_metadata,
        "result": "passed",
    }
    (output / "run.json").write_text(
        json.dumps(result_meta, indent=2) + "\n", encoding="utf-8"
    )

    print(f"[+] Wrote {output / 'latency_cbr.csv'}")
    print(f"[+] Wrote {output / 'latency_poisson.csv'}")
    print(f"[+] Wrote {output / 'latency_summary.csv'}")
    print(f"[+] Wrote latency plots under {output}")

    for summary in summaries:
        print(
            f"[+] {summary['mode']}: n={summary['n']}, "
            f"mean={summary['mean_ms']:.3f} ms, "
            f"median={summary['median_ms']:.3f} ms, "
            f"p95={summary['p95_ms']:.3f} ms, "
            f"stddev={summary['stddev_ms']:.3f} ms, "
            f"min={summary['min_ms']:.3f} ms, max={summary['max_ms']:.3f} ms"
        )

    print("\nBT-06 functional execution: PASS")
    print("[i] Latency values are experimental results; no maximum latency is a PASS/FAIL gate.")
    print(f"[i] Results directory: {output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] BT-06 analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
