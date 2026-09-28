#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
import sys
from dataclasses import dataclass
from datetime import datetime
from pathlib import Path

import matplotlib.pyplot as plt

PROJECT_ROOT = Path(__file__).resolve().parents[2]
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


def read_json(path: Path) -> dict:
    if not path.exists():
        raise Bt06Error(f"Missing required file: {path}")
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as exc:
        raise Bt06Error(f"Invalid JSON in {path}: {exc}") from exc


def percentile(values: list[float], q: float) -> float:
    ordered = sorted(values)
    if not ordered:
        raise Bt06Error("Cannot calculate percentile of empty sample")
    if len(ordered) == 1:
        return ordered[0]
    pos = (len(ordered) - 1) * q
    lo = math.floor(pos)
    hi = math.ceil(pos)
    if lo == hi:
        return ordered[lo]
    frac = pos - lo
    return ordered[lo] * (1.0 - frac) + ordered[hi] * frac


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
        raise Bt06Error(f"Invalid clock calibration for {phase}/{client}") from exc


def correct_vm_time(vm_event_ns: int, pre: Anchor, post: Anchor) -> CorrectedTime:
    if post.vm_ns <= pre.vm_ns:
        raise Bt06Error("Post calibration is not later than pre calibration")
    alpha = (vm_event_ns - pre.vm_ns) / (post.vm_ns - pre.vm_ns)
    if alpha < -0.05 or alpha > 1.05:
        raise Bt06Error("Message timestamp lies materially outside calibration window")
    alpha = min(1.0, max(0.0, alpha))
    offset = pre.offset_vm_minus_host_ns + alpha * (
        post.offset_vm_minus_host_ns - pre.offset_vm_minus_host_ns
    )
    rtt = pre.rtt_ns + alpha * (post.rtt_ns - pre.rtt_ns)
    return CorrectedTime(
        host_ns=int(round(vm_event_ns - offset)),
        offset_ns=offset,
        uncertainty_ns=max(0.0, rtt / 2.0),
    )


def index_events(entries: list[dict], label: str) -> dict[str, dict]:
    indexed: dict[str, dict] = {}
    for entry in entries:
        try:
            msg_id = str(entry["msg_id"]).lower()
            timestamp_ns = int(entry["timestamp_ns"])
            sequence = int(entry["sequence"])
        except (KeyError, TypeError, ValueError) as exc:
            raise Bt06Error(f"Malformed {label} event: {entry!r}") from exc
        if msg_id in indexed:
            raise Bt06Error(f"Duplicate {label} event for {msg_id}")
        indexed[msg_id] = {**entry, "timestamp_ns": timestamp_ns, "sequence": sequence}
    return indexed


def validate_mode(mode_dir: Path) -> tuple[dict, dict, dict]:
    events = read_json(mode_dir / "message_events.json")
    calibration = read_json(mode_dir / "clock_calibration.json")
    run_meta = read_json(mode_dir / "run.json")

    if events.get("series_policy") != "ack-gated" or run_meta.get("series_policy") != "ack-gated":
        raise Bt06Error(f"{mode_dir.name}: source series is not ACK-gated")
    if not events.get("validation", {}).get("verified_no_overlap", False):
        raise Bt06Error(f"{mode_dir.name}: no-overlap validation is missing or false")

    expected = int(run_meta.get("messages_requested", 0))
    if expected <= 0:
        raise Bt06Error(f"{mode_dir.name}: invalid messages_requested")

    queue = index_events(events.get("queue", []), "QUEUE")
    tx = index_events(events.get("tx", []), "TX")
    rx = index_events(events.get("rx", []), "RX")
    ack = index_events(events.get("ack", []), "ACK")

    if not (len(queue) == len(tx) == len(rx) == len(ack) == expected):
        raise Bt06Error(
            f"{mode_dir.name}: expected {expected} events of each type, got "
            f"QUEUE={len(queue)} TX={len(tx)} RX={len(rx)} ACK={len(ack)}"
        )
    if not (set(queue) == set(tx) == set(rx) == set(ack)):
        raise Bt06Error(f"{mode_dir.name}: QUEUE/TX/RX/ACK ID sets differ")

    ordered = sorted(queue.values(), key=lambda item: item["sequence"])
    if [item["sequence"] for item in ordered] != list(range(1, expected + 1)):
        raise Bt06Error(f"{mode_dir.name}: invalid sequence numbering")
    ack_by_id = ack
    for i in range(expected - 1):
        current_id = ordered[i]["msg_id"].lower()
        next_id = ordered[i + 1]["msg_id"].lower()
        if int(queue[next_id]["timestamp_ns"]) < int(ack_by_id[current_id]["timestamp_ns"]):
            raise Bt06Error(f"{mode_dir.name}: ACK-gating invariant violated at sequence {i+1}")

    return events, calibration, run_meta


def build_rows(root: Path, mode: str) -> tuple[list[dict], dict]:
    mode_dir = root / mode
    events, calibration, run_meta = validate_mode(mode_dir)
    queue = index_events(events["queue"], "QUEUE")
    tx = index_events(events["tx"], "TX")
    rx = index_events(events["rx"], "RX")
    ack = index_events(events["ack"], "ACK")

    a_pre = anchor_from(calibration, "pre", "client_a")
    a_post = anchor_from(calibration, "post", "client_a")
    b_pre = anchor_from(calibration, "pre", "client_b")
    b_post = anchor_from(calibration, "post", "client_b")

    rows: list[dict] = []
    for msg_id, q_event in sorted(queue.items(), key=lambda item: item[1]["sequence"]):
        q = correct_vm_time(int(q_event["timestamp_ns"]), a_pre, a_post)
        t = correct_vm_time(int(tx[msg_id]["timestamp_ns"]), a_pre, a_post)
        r = correct_vm_time(int(rx[msg_id]["timestamp_ns"]), b_pre, b_post)
        a = correct_vm_time(int(ack[msg_id]["timestamp_ns"]), a_pre, a_post)

        e2e_ns = r.host_ns - q.host_ns
        scheduler_ns = t.host_ns - q.host_ns
        post_tx_ns = r.host_ns - t.host_ns
        ack_ns = a.host_ns - q.host_ns
        if min(e2e_ns, scheduler_ns, post_tx_ns, ack_ns) < 0:
            raise Bt06Error(f"{mode}: negative corrected latency component for {msg_id}")
        if abs(e2e_ns - (scheduler_ns + post_tx_ns)) > 2_000_000:
            raise Bt06Error(f"{mode}: latency decomposition mismatch for {msg_id}")

        rows.append({
            "sequence": int(q_event["sequence"]),
            "message_id": msg_id,
            "prequeue_jitter_ms": float(q_event.get("prequeue_jitter_seconds", 0.0)) * 1000.0,
            "t_queue_vm_ns": int(q_event["timestamp_ns"]),
            "t_tx_vm_ns": int(tx[msg_id]["timestamp_ns"]),
            "t_rx_vm_ns": int(rx[msg_id]["timestamp_ns"]),
            "t_ack_vm_ns": int(ack[msg_id]["timestamp_ns"]),
            "t_queue_host_corrected_ns": q.host_ns,
            "t_tx_host_corrected_ns": t.host_ns,
            "t_rx_host_corrected_ns": r.host_ns,
            "t_ack_host_corrected_ns": a.host_ns,
            "latency_ms": e2e_ns / 1e6,
            "queue_to_tx_ms": scheduler_ns / 1e6,
            "tx_to_rx_ms": post_tx_ns / 1e6,
            "queue_to_ack_ms": ack_ns / 1e6,
            "sender_offset_ms": q.offset_ns / 1e6,
            "receiver_offset_ms": r.offset_ns / 1e6,
            "estimated_clock_uncertainty_ms": (q.uncertainty_ns + r.uncertainty_ns) / 1e6,
        })

    metadata = {
        "mode": mode,
        "run": run_meta,
        "clock": {
            "client_a_pre_offset_ms": a_pre.offset_vm_minus_host_ns / 1e6,
            "client_a_post_offset_ms": a_post.offset_vm_minus_host_ns / 1e6,
            "client_a_offset_drift_ms": (a_post.offset_vm_minus_host_ns - a_pre.offset_vm_minus_host_ns) / 1e6,
            "client_b_pre_offset_ms": b_pre.offset_vm_minus_host_ns / 1e6,
            "client_b_post_offset_ms": b_post.offset_vm_minus_host_ns / 1e6,
            "client_b_offset_drift_ms": (b_post.offset_vm_minus_host_ns - b_pre.offset_vm_minus_host_ns) / 1e6,
            "client_a_pre_rtt_ms": a_pre.rtt_ns / 1e6,
            "client_a_post_rtt_ms": a_post.rtt_ns / 1e6,
            "client_b_pre_rtt_ms": b_pre.rtt_ns / 1e6,
            "client_b_post_rtt_ms": b_post.rtt_ns / 1e6,
        },
    }
    return rows, metadata


def stats(values: list[float], prefix: str = "") -> dict:
    return {
        f"{prefix}mean_ms": statistics.fmean(values),
        f"{prefix}median_ms": statistics.median(values),
        f"{prefix}p95_ms": percentile(values, 0.95),
        f"{prefix}stddev_ms": statistics.stdev(values) if len(values) > 1 else 0.0,
        f"{prefix}min_ms": min(values),
        f"{prefix}max_ms": max(values),
    }


def summarize(mode: str, rows: list[dict], metadata: dict) -> dict:
    primary = [float(r["latency_ms"]) for r in rows]
    qtx = [float(r["queue_to_tx_ms"]) for r in rows]
    txrx = [float(r["tx_to_rx_ms"]) for r in rows]
    uncertainty = [float(r["estimated_clock_uncertainty_ms"]) for r in rows]
    return {
        "mode": mode,
        "n": len(rows),
        **stats(primary),
        **stats(qtx, "queue_to_tx_"),
        **stats(txrx, "tx_to_rx_"),
        "mean_estimated_clock_uncertainty_ms": statistics.fmean(uncertainty),
        "max_estimated_clock_uncertainty_ms": max(uncertainty),
        **metadata["clock"],
    }


def write_latency_csv(path: Path, rows: list[dict]) -> None:
    fields = list(rows[0].keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def write_summary_csv(path: Path, summaries: list[dict]) -> None:
    fields = list(summaries[0].keys())
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fields)
        writer.writeheader()
        writer.writerows(summaries)


def write_plots(root: Path, mode_rows: dict[str, list[dict]]) -> None:
    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in MODES:
        ax.hist([r["latency_ms"] for r in mode_rows[mode]], bins=40, alpha=0.5, label=mode.upper())
    ax.set_xlabel("End-to-end latency QUEUE→RX [ms]")
    ax.set_ylabel("Messages")
    ax.set_title("BT-06 latency distribution")
    ax.legend()
    fig.tight_layout()
    fig.savefig(root / "latency_histogram.png", dpi=160)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(8, 5))
    for mode in MODES:
        values = sorted(float(r["latency_ms"]) for r in mode_rows[mode])
        n = len(values)
        ax.plot(values, [(i + 1) / n for i in range(n)], label=mode.upper())
    ax.set_xlabel("End-to-end latency QUEUE→RX [ms]")
    ax.set_ylabel("ECDF")
    ax.set_ylim(0.0, 1.0)
    ax.set_title("BT-06 latency ECDF")
    ax.legend()
    fig.tight_layout()
    fig.savefig(root / "latency_ecdf.png", dpi=160)
    plt.close(fig)

    fig, ax = plt.subplots(figsize=(7, 5))
    ax.boxplot(
        [[r["latency_ms"] for r in mode_rows["cbr"]], [r["latency_ms"] for r in mode_rows["poisson"]]],
        tick_labels=["CBR", "Poisson"],
        showfliers=True,
    )
    ax.set_ylabel("End-to-end latency QUEUE→RX [ms]")
    ax.set_title("BT-06 latency comparison")
    fig.tight_layout()
    fig.savefig(root / "latency_boxplot.png", dpi=160)
    plt.close(fig)


def main() -> int:
    parser = argparse.ArgumentParser(description="Analyze one completed ACK-gated BT-06 run")
    parser.add_argument("run", type=Path)
    args = parser.parse_args()
    root = args.run.resolve()
    suite = read_json(root / "suite_run.json")
    if suite.get("series_policy") != "ack-gated":
        raise Bt06Error("BT-06 run is not marked series_policy=ack-gated")
    if suite.get("result") != "passed":
        raise Bt06Error("BT-06 source run did not finish with result=passed")
    if set(suite.get("completed", [])) != {"cbr", "poisson"}:
        raise Bt06Error("Analysis requires completed CBR and Poisson modes")

    mode_rows: dict[str, list[dict]] = {}
    mode_meta: dict[str, dict] = {}
    summaries: list[dict] = []
    validation: dict[str, dict] = {}

    for mode in MODES:
        rows, metadata = build_rows(root, mode)
        mode_rows[mode] = rows
        mode_meta[mode] = metadata
        summaries.append(summarize(mode, rows, metadata))
        write_latency_csv(root / f"latency_{mode}.csv", rows)
        validation[mode] = {
            "series_policy": "ack-gated",
            "expected_messages": len(rows),
            "queue_events": len(rows),
            "tx_events": len(rows),
            "rx_events": len(rows),
            "ack_events": len(rows),
            "verified_no_overlap": True,
        }

    write_summary_csv(root / "latency_summary.csv", summaries)
    write_plots(root, mode_rows)

    result = {
        "created_at": datetime.now().astimezone().isoformat(),
        "commit": suite.get("commit"),
        "dirty_worktree": suite.get("dirty_worktree"),
        "series_policy": "ack-gated",
        "primary_latency_definition": "QUEUE(sender) -> RX(receiver), corrected to host clock",
        "queue_to_tx_definition": "QUEUE(sender) -> TX(sender): traffic-shaper scheduling delay",
        "tx_to_rx_definition": "TX(sender) -> RX(receiver): post-transmission delivery delay",
        "phase_randomization": suite.get("phase_randomization"),
        "phase_jitter_seed": suite.get("phase_jitter_seed"),
        "workload_validation": validation,
        "clock_calibration": {mode: mode_meta[mode]["clock"] for mode in MODES},
        "result": "passed",
    }
    (root / "run.json").write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")

    print(f"[+] Wrote {root / 'latency_cbr.csv'}")
    print(f"[+] Wrote {root / 'latency_poisson.csv'}")
    print(f"[+] Wrote {root / 'latency_summary.csv'}")
    print(f"[+] Wrote latency plots under {root}")
    for s in summaries:
        print(
            f"[+] {s['mode']}: n={s['n']}, mean={s['mean_ms']:.3f} ms, "
            f"median={s['median_ms']:.3f} ms, p95={s['p95_ms']:.3f} ms, "
            f"stddev={s['stddev_ms']:.3f} ms, min={s['min_ms']:.3f} ms, max={s['max_ms']:.3f} ms"
        )
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:
        print(f"[!] BT-06 analysis failed: {exc}", file=sys.stderr)
        raise SystemExit(1)
