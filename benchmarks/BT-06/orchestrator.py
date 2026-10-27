#!/usr/bin/env python3
from __future__ import annotations

import argparse
import importlib.util
import json
import random
import re
import shlex
import shutil
import subprocess
import sys
import time
from dataclasses import dataclass, asdict
from datetime import datetime
from pathlib import Path

PROJECT_ROOT = Path(__file__).resolve().parents[2]
BENCHMARKS_DIR = PROJECT_ROOT / "benchmarks"
BT05_ORCH = BENCHMARKS_DIR / "BT-05" / "orchestrator.py"
ANALYZE = BENCHMARKS_DIR / "BT-06" / "analyze.py"
RESULTS_DIR = BENCHMARKS_DIR / "results" / "BT-06"

DEFAULT_MESSAGES = 1000
DEFAULT_MESSAGE_SIZE = 150
DEFAULT_CBR_INTERVAL_MS = 10_000
DEFAULT_POISSON_LAMBDA = 0.1
DEFAULT_PHASE_JITTER_SEED = 260925
DEFAULT_PER_MESSAGE_TIMEOUT_SECONDS = 300.0
DEFAULT_PROGRESS_EVERY = 10

QUEUE_RE = re.compile(r"BT05_EVENT QUEUE timestamp_ns=(\d+) msg_id=([0-9a-fA-F]+)")
RX_RE = re.compile(r"BT05_EVENT RX timestamp_ns=(\d+) msg_id=([0-9a-fA-F]+)")
ACK_RE = re.compile(r"BT05_EVENT ACK timestamp_ns=(\d+) msg_id=([0-9a-fA-F]+)")
MAP_RE = re.compile(r"BT06_EVENT MAP frame_hash=([0-9a-fA-F]+) msg_id=([0-9a-fA-F]+)")
TX_RE = re.compile(
    r"BT06_EVENT TX timestamp_ns=(\d+) action=(\d+) bytes=(\d+) frame_hash=([0-9a-fA-F]+)"
)


class Bt06Error(RuntimeError):
    pass


def load_bt05_module():
    if not BT05_ORCH.exists():
        raise Bt06Error(f"Missing helper module: {BT05_ORCH}")
    spec = importlib.util.spec_from_file_location("blankchat_bt05_helpers", BT05_ORCH)
    if spec is None or spec.loader is None:
        raise Bt06Error("Could not load BT-05 helper module")
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


b5 = load_bt05_module()


@dataclass
class ModeRun:
    started_at: str
    finished_at: str = ""
    mode: str = ""
    commit: str = ""
    dirty_worktree: bool = False
    messages_requested: int = DEFAULT_MESSAGES
    message_size: int = DEFAULT_MESSAGE_SIZE
    cbr_interval_ms: int = DEFAULT_CBR_INTERVAL_MS
    poisson_lambda: float = DEFAULT_POISSON_LAMBDA
    phase_jitter_seed: int = DEFAULT_PHASE_JITTER_SEED
    phase_window_seconds: float = 0.0
    series_policy: str = "ack-gated"
    client_a_ip: str = ""
    client_b_ip: str = ""
    server_ip: str = ""
    onion: str = ""
    messages_completed: int = 0
    queue_events: int = 0
    tx_events: int = 0
    rx_events: int = 0
    ack_events: int = 0
    unique_message_ids: int = 0
    verified_no_overlap: bool = False
    duration_seconds: float = 0.0
    result: str = "incomplete"


def info(msg: str) -> None:
    print(f"[i] {msg}", flush=True)


def ok(msg: str) -> None:
    print(f"[+] {msg}", flush=True)


def warn(msg: str) -> None:
    print(f"[!] {msg}", file=sys.stderr, flush=True)


def require_dependencies() -> None:
    required = ("virsh", "virt-install", "qemu-img", "socat", "sudo", "iptables", "pkill")
    missing = [name for name in required if shutil.which(name) is None]
    if missing:
        raise Bt06Error("Missing host dependencies: " + ", ".join(missing))
    for path in (b5.VM_MANAGER, b5.CHUTNEY_MANAGER, ANALYZE):
        if not path.exists():
            raise Bt06Error(f"Required file does not exist: {path}")


def assert_client_binary_has_diagnostics(child, vm_name: str) -> None:
    markers = (
        "BT05_EVENT QUEUE",
        "BT05_EVENT RX",
        "BT05_EVENT ACK",
        "BT06_EVENT MAP",
        "BT06_EVENT TX",
        "BT06_EVENT CLOCK",
    )
    for marker in markers:
        _, rc = b5.console_cmd(
            child,
            f"grep -q {shlex.quote(marker)} /usr/bin/blank_chat_client 2>/dev/null",
            check=False,
        )
        if rc != 0:
            raise Bt06Error(
                f"{vm_name} does not contain required BT-06 diagnostics. "
                "Run './benchmarks/BT-06/run.sh --prepare --messages 20 "
                "--cbr-interval-ms 1000 --poisson-lambda 1.0' first."
            )


def parse_events(text: str, regex: re.Pattern[str]) -> list[tuple[int, str]]:
    events: list[tuple[int, str]] = []
    for line in b5.strip_ansi(text).splitlines():
        match = regex.search(line)
        if match:
            events.append((int(match.group(1)), match.group(2).lower()))
    return events


def parse_maps(text: str) -> list[tuple[str, str]]:
    out: list[tuple[str, str]] = []
    for line in b5.strip_ansi(text).splitlines():
        m = MAP_RE.search(line)
        if m:
            out.append((m.group(1).lower(), m.group(2).lower()))
    return out


def parse_tx(text: str) -> list[tuple[int, int, int, str]]:
    out: list[tuple[int, int, int, str]] = []
    for line in b5.strip_ansi(text).splitlines():
        m = TX_RE.search(line)
        if m:
            out.append((int(m.group(1)), int(m.group(2)), int(m.group(3)), m.group(4).lower()))
    return out


def exact_payload(sequence: int, size: int) -> str:
    prefix = "BT06_MSG_"
    seq = f"{sequence:06d}"
    filler_len = size - len(prefix) - len(seq) - 1
    if filler_len < 1:
        raise Bt06Error("Message size is too small for BT-06 sequence payload")
    return f"{prefix}{seq}_{'X' * filler_len}"


def wait_for_ack_increment(child, before: int, timeout: float) -> None:
    try:
        b5.wait_client_log_count(child, "BT05_EVENT ACK", before + 1, timeout)
    except Exception as exc:
        raise Bt06Error(f"Timed out waiting for ACK after {timeout:.1f}s") from exc


def run_ack_gated_series(
    sender,
    receiver,
    *,
    mode: str,
    messages: int,
    message_size: int,
    cbr_interval_ms: int,
    poisson_lambda: float,
    phase_seed: int,
    per_message_timeout: float,
    progress_every: int,
) -> list[dict]:
    mean_tick = cbr_interval_ms / 1000.0 if mode == "cbr" else 1.0 / poisson_lambda
    phase_window = mean_tick
    rng = random.Random(phase_seed + (0 if mode == "cbr" else 1_000_003))
    controls: list[dict] = []
    series_start = time.monotonic()

    info(
        f"Running ACK-gated BT-06 series: {messages} messages, mode={mode}, "
        f"phase window=[0,{phase_window:.3f})s"
    )

    for sequence in range(1, messages + 1):
        jitter_s = rng.random() * phase_window
        time.sleep(jitter_s)

        queue_before = b5.client_log_count(sender.console, "BT05_EVENT QUEUE")
        ack_before = b5.client_log_count(sender.console, "BT05_EVENT ACK")
        payload = exact_payload(sequence, message_size)
        b5.client_command(sender.console, f"send {sender.peer_alias} {payload}")
        b5.wait_client_log_count(
            sender.console,
            "BT05_EVENT QUEUE",
            queue_before + 1,
            min(60.0, per_message_timeout),
        )
        wait_for_ack_increment(sender.console, ack_before, per_message_timeout)

        controls.append({
            "sequence": sequence,
            "prequeue_jitter_seconds": jitter_s,
        })

        if sequence == 1 or sequence == messages or sequence % progress_every == 0:
            elapsed = time.monotonic() - series_start
            info(f"Progress: {sequence}/{messages} ACKed, elapsed={elapsed/60.0:.1f} min")

        if not b5.client_alive(sender.console) or not b5.client_alive(receiver.console):
            raise Bt06Error("A client exited during the ACK-gated series")

    ok(f"ACK-gated series complete: {messages}/{messages}")
    return controls


def collect_and_validate_events(sender, receiver, expected: int, controls: list[dict]) -> dict:
    sender_log = b5.remote_cat(sender.console, b5.REMOTE_CLIENT_LOG)
    receiver_log = b5.remote_cat(receiver.console, b5.REMOTE_CLIENT_LOG)

    queued = parse_events(sender_log, QUEUE_RE)
    received = parse_events(receiver_log, RX_RE)
    acked = parse_events(sender_log, ACK_RE)
    frame_maps = parse_maps(sender_log)
    tx_events = parse_tx(sender_log)

    if len(queued) != expected or len(received) != expected or len(acked) != expected:
        raise Bt06Error(
            f"Expected {expected} QUEUE/RX/ACK events, got "
            f"QUEUE={len(queued)}, RX={len(received)}, ACK={len(acked)}"
        )
    if len(frame_maps) != expected:
        raise Bt06Error(f"Expected {expected} MAP events, got {len(frame_maps)}")

    queue_ids = [msg_id for _, msg_id in queued]
    rx_ids = [msg_id for _, msg_id in received]
    ack_ids = [msg_id for _, msg_id in acked]
    mapped_ids = [msg_id for _, msg_id in frame_maps]
    frame_hashes = [frame_hash for frame_hash, _ in frame_maps]

    if len(set(queue_ids)) != expected:
        raise Bt06Error("QUEUE message IDs are not unique")
    if len(set(frame_hashes)) != expected:
        raise Bt06Error("MAP frame hashes are not unique")
    if not (set(queue_ids) == set(rx_ids) == set(ack_ids) == set(mapped_ids)):
        raise Bt06Error("QUEUE/MAP/RX/ACK message-ID sets differ")

    hash_to_msg = {frame_hash: msg_id for frame_hash, msg_id in frame_maps}
    transmitted: list[tuple[int, str, str]] = []
    for ts, action, _bytes, frame_hash in tx_events:
        if action != 1:
            continue
        msg_id = hash_to_msg.get(frame_hash)
        if msg_id is not None:
            transmitted.append((ts, msg_id, frame_hash))

    if len(transmitted) != expected:
        raise Bt06Error(f"Expected {expected} matched PUSH TX events, got {len(transmitted)}")
    if len({frame_hash for _, _, frame_hash in transmitted}) != expected:
        raise Bt06Error("A benchmark frame was transmitted more than once")

    queue_by_id = {msg_id: ts for ts, msg_id in queued}
    ack_by_id = {msg_id: ts for ts, msg_id in acked}
    ordered_ids = [msg_id for _, msg_id in sorted(queued)]

    if len(controls) != expected:
        raise Bt06Error("Control-record count differs from message count")

    no_overlap = True
    for i in range(expected - 1):
        current_id = ordered_ids[i]
        next_id = ordered_ids[i + 1]
        if queue_by_id[next_id] < ack_by_id[current_id]:
            no_overlap = False
            break
    if not no_overlap:
        raise Bt06Error("ACK-gating invariant failed: QUEUE(i+1) occurred before ACK(i)")

    seq_by_id = {msg_id: i + 1 for i, msg_id in enumerate(ordered_ids)}
    jitter_by_seq = {int(item["sequence"]): float(item["prequeue_jitter_seconds"]) for item in controls}

    payload = {
        "series_policy": "ack-gated",
        "queue": [
            {
                "sequence": seq_by_id[msg_id],
                "timestamp_ns": ts,
                "msg_id": msg_id,
                "prequeue_jitter_seconds": jitter_by_seq[seq_by_id[msg_id]],
            }
            for ts, msg_id in sorted(queued)
        ],
        "tx": [
            {
                "sequence": seq_by_id[msg_id],
                "timestamp_ns": ts,
                "msg_id": msg_id,
                "frame_hash": frame_hash,
            }
            for ts, msg_id, frame_hash in sorted(transmitted)
        ],
        "rx": [
            {"sequence": seq_by_id[msg_id], "timestamp_ns": ts, "msg_id": msg_id}
            for ts, msg_id in sorted(received)
        ],
        "ack": [
            {"sequence": seq_by_id[msg_id], "timestamp_ns": ts, "msg_id": msg_id}
            for ts, msg_id in sorted(acked)
        ],
        "validation": {
            "expected": expected,
            "queue": len(queued),
            "tx": len(transmitted),
            "rx": len(received),
            "ack": len(acked),
            "unique_message_ids": len(set(queue_ids)),
            "verified_no_overlap": True,
            "invariant": "QUEUE(i+1) >= ACK(i) on sender VM clock",
        },
    }
    return payload


def write_mode_logs(mode_dir: Path, sender, receiver, server_console) -> None:
    try:
        (mode_dir / "client_a.log").write_text(b5.remote_cat(sender.console, b5.REMOTE_CLIENT_LOG), encoding="utf-8")
    except Exception as exc:
        warn(f"Could not collect client A log: {exc}")
    try:
        (mode_dir / "client_b.log").write_text(b5.remote_cat(receiver.console, b5.REMOTE_CLIENT_LOG), encoding="utf-8")
    except Exception as exc:
        warn(f"Could not collect client B log: {exc}")
    try:
        (mode_dir / "server.log").write_text(b5.remote_cat(server_console, b5.REMOTE_SERVER_LOG), encoding="utf-8")
    except Exception as exc:
        warn(f"Could not collect server log: {exc}")


def run_mode(
    *,
    mode: str,
    messages: int,
    message_size: int,
    cbr_interval_ms: int,
    poisson_lambda: float,
    phase_seed: int,
    per_message_timeout: float,
    progress_every: int,
    commit: str,
    dirty: bool,
    server_ip: str,
    client_a_ip: str,
    client_b_ip: str,
    client_tor,
    server_console,
    client_a_console,
    client_b_console,
    mode_dir: Path,
) -> None:
    mode_dir.mkdir(parents=True, exist_ok=True)
    info(f"=== BT-06 mode: {mode} ({messages} x {message_size} B, ACK-gated) ===")

    run_meta = ModeRun(
        started_at=datetime.now().astimezone().isoformat(),
        mode=mode,
        commit=commit,
        dirty_worktree=dirty,
        messages_requested=messages,
        message_size=message_size,
        cbr_interval_ms=cbr_interval_ms,
        poisson_lambda=poisson_lambda,
        phase_jitter_seed=phase_seed,
        phase_window_seconds=(cbr_interval_ms / 1000.0 if mode == "cbr" else 1.0 / poisson_lambda),
        client_a_ip=client_a_ip,
        client_b_ip=client_b_ip,
        server_ip=server_ip,
    )

    sender = b5.ClientHandle("A", b5.CLIENT_A_VM, "bob", client_a_console)
    receiver = b5.ClientHandle("B", b5.CLIENT_B_VM, "alice", client_b_console)
    calibration = {
        "method": (
            "Each endpoint is sampled against the same host wall clock before and after the "
            "measured series; BT-06 linearly interpolates VM-minus-host offset between "
            "minimum-RTT calibration samples."
        ),
        "samples_per_point": 9,
        "pre": {},
        "post": {},
    }
    started = time.monotonic()

    try:
        b5.configure_server(server_console)
        b5.start_server(server_console)
        onion = b5.wait_for_onion(server_console)
        b5.wait_for_hidden_service(onion, client_tor.socks_port, timeout=180.0)
        run_meta.onion = onion

        b5.configure_client(client_a_console, onion, mode, cbr_interval_ms, poisson_lambda)
        b5.configure_client(client_b_console, onion, mode, cbr_interval_ms, poisson_lambda)
        b5.start_client(client_a_console, b5.CLIENT_A_VM)
        b5.start_client(client_b_console, b5.CLIENT_B_VM)

        mnemonic_a = b5.extract_mnemonic(client_a_console)
        mnemonic_b = b5.extract_mnemonic(client_b_console)
        b5.add_contact(client_a_console, "bob", mnemonic_b)
        b5.add_contact(client_b_console, "alice", mnemonic_a)
        b5.connect_client(client_a_console, mode)
        b5.connect_client(client_b_console, mode)
        b5.wait_for_initial_pfs(sender, receiver)

        info("Calibrating VM clocks against the host before the measured series...")
        calibration["pre"]["client_a"] = b5.calibrate_vm_clock(client_a_console, b5.CLIENT_A_VM)
        calibration["pre"]["client_b"] = b5.calibrate_vm_clock(client_b_console, b5.CLIENT_B_VM)

        for marker, child in (
            ("BT05_EVENT QUEUE", client_a_console),
            ("BT05_EVENT RX", client_b_console),
            ("BT05_EVENT ACK", client_a_console),
            ("BT06_EVENT MAP", client_a_console),
        ):
            if b5.client_log_count(child, marker) != 0:
                raise Bt06Error(f"Unexpected pre-measurement diagnostic event: {marker}")

        controls = run_ack_gated_series(
            sender,
            receiver,
            mode=mode,
            messages=messages,
            message_size=message_size,
            cbr_interval_ms=cbr_interval_ms,
            poisson_lambda=poisson_lambda,
            phase_seed=phase_seed,
            per_message_timeout=per_message_timeout,
            progress_every=progress_every,
        )

        info("Calibrating VM clocks against the host after the measured series...")
        calibration["post"]["client_a"] = b5.calibrate_vm_clock(client_a_console, b5.CLIENT_A_VM)
        calibration["post"]["client_b"] = b5.calibrate_vm_clock(client_b_console, b5.CLIENT_B_VM)

        events = collect_and_validate_events(sender, receiver, messages, controls)
        (mode_dir / "message_events.json").write_text(json.dumps(events, indent=2) + "\n", encoding="utf-8")
        (mode_dir / "clock_calibration.json").write_text(json.dumps(calibration, indent=2) + "\n", encoding="utf-8")

        validation = events["validation"]
        run_meta.messages_completed = messages
        run_meta.queue_events = validation["queue"]
        run_meta.tx_events = validation["tx"]
        run_meta.rx_events = validation["rx"]
        run_meta.ack_events = validation["ack"]
        run_meta.unique_message_ids = validation["unique_message_ids"]
        run_meta.verified_no_overlap = bool(validation["verified_no_overlap"])
        run_meta.result = "passed"
        ok("QUEUE/TX/RX/ACK sets agree and ACK-gating invariant is verified")
    finally:
        run_meta.finished_at = datetime.now().astimezone().isoformat()
        run_meta.duration_seconds = time.monotonic() - started
        write_mode_logs(mode_dir, sender, receiver, server_console)
        (mode_dir / "clock_calibration.json").write_text(json.dumps(calibration, indent=2) + "\n", encoding="utf-8")
        (mode_dir / "run.json").write_text(json.dumps(asdict(run_meta), indent=2) + "\n", encoding="utf-8")
        try:
            b5.stop_client(client_a_console)
        except Exception:
            pass
        try:
            b5.stop_client(client_b_console)
        except Exception:
            pass
        try:
            b5.stop_server(server_console)
        except Exception:
            pass


def parse_args() -> argparse.Namespace:
    p = argparse.ArgumentParser(description="BT-06 isolated end-to-end latency benchmark for CBR and Poisson")
    p.add_argument("--mode", choices=["all", "cbr", "poisson"], default="all")
    p.add_argument("--messages", type=int, default=DEFAULT_MESSAGES)
    p.add_argument("--message-size", type=int, default=DEFAULT_MESSAGE_SIZE)
    p.add_argument("--cbr-interval-ms", type=int, default=DEFAULT_CBR_INTERVAL_MS)
    p.add_argument("--poisson-lambda", type=float, default=DEFAULT_POISSON_LAMBDA)
    p.add_argument("--phase-jitter-seed", type=int, default=DEFAULT_PHASE_JITTER_SEED)
    p.add_argument("--per-message-timeout-seconds", type=float, default=DEFAULT_PER_MESSAGE_TIMEOUT_SECONDS)
    p.add_argument("--progress-every", type=int, default=DEFAULT_PROGRESS_EVERY)
    p.add_argument("--skip-analysis", action="store_true")
    p.add_argument("--keep-environment", action="store_true")
    args = p.parse_args()
    if not 1 <= args.messages <= 999999:
        p.error("--messages must be in [1, 999999]")
    if args.message_size < 32:
        p.error("--message-size must be >= 32")
    if args.cbr_interval_ms <= 0 or args.poisson_lambda <= 0:
        p.error("scheduler parameters must be > 0")
    if args.per_message_timeout_seconds <= 0 or args.progress_every <= 0:
        p.error("timeout/progress parameters must be > 0")
    return args


def main() -> int:
    args = parse_args()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_root = RESULTS_DIR / stamp
    result_root.mkdir(parents=True, exist_ok=True)

    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROJECT_ROOT, text=True, capture_output=True, check=True).stdout.strip()
    git_status = subprocess.run(["git", "status", "--short"], cwd=PROJECT_ROOT, text=True, capture_output=True, check=True).stdout
    dirty = bool(git_status.strip())
    (result_root / "commit.txt").write_text(commit + "\n", encoding="utf-8")
    (result_root / "git_status.txt").write_text(git_status, encoding="utf-8")

    modes = ["cbr", "poisson"] if args.mode == "all" else [args.mode]
    server_console = client_a_console = client_b_console = None
    control_socat = service_socat = socks_socat = None
    firewall_added = False
    chutney_started = False
    client_tor = None
    completed: list[str] = []
    suite_result = "failed"

    try:
        require_dependencies()
        b5.acquire_sudo()
        b5.repair_workspace_permissions()
        b5.cleanup_stale_chutney_tor()
        b5.recreate_vms()

        server_ip = b5.wait_for_vm_ip(b5.SERVER_VM)
        client_a_ip = b5.wait_for_vm_ip(b5.CLIENT_A_VM)
        client_b_ip = b5.wait_for_vm_ip(b5.CLIENT_B_VM)

        b5.start_chutney()
        chutney_started = True
        client_tor = b5.discover_dedicated_client_tor()
        b5.check_socks5(client_tor.socks_port)
        firewall_added = b5.ensure_firewall_rule()
        b5.stop_stale_socat()

        control_socat = b5.start_socat(
            [f"TCP-LISTEN:{b5.SERVER_TOR_CONTROL_PORT},bind={b5.GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{b5.SERVER_TOR_CONTROL_PORT}"],
            result_root / "socat_control.log",
        )
        service_socat = b5.start_socat(
            [f"TCP-LISTEN:{b5.SERVER_PORT},bind=127.0.0.1,fork,reuseaddr", f"TCP:{server_ip}:{b5.SERVER_PORT}"],
            result_root / "socat_service.log",
        )
        socks_socat = b5.start_socat(
            [f"TCP-LISTEN:{b5.VM_TOR_SOCKS_PORT},bind={b5.GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{client_tor.socks_port}"],
            result_root / "socat_socks.log",
        )

        server_console = b5.login_to_console(b5.SERVER_VM)
        client_a_console = b5.login_to_console(b5.CLIENT_A_VM)
        client_b_console = b5.login_to_console(b5.CLIENT_B_VM)
        assert_client_binary_has_diagnostics(client_a_console, b5.CLIENT_A_VM)
        assert_client_binary_has_diagnostics(client_b_console, b5.CLIENT_B_VM)

        for mode in modes:
            run_mode(
                mode=mode,
                messages=args.messages,
                message_size=args.message_size,
                cbr_interval_ms=args.cbr_interval_ms,
                poisson_lambda=args.poisson_lambda,
                phase_seed=args.phase_jitter_seed,
                per_message_timeout=args.per_message_timeout_seconds,
                progress_every=args.progress_every,
                commit=commit,
                dirty=dirty,
                server_ip=server_ip,
                client_a_ip=client_a_ip,
                client_b_ip=client_b_ip,
                client_tor=client_tor,
                server_console=server_console,
                client_a_console=client_a_console,
                client_b_console=client_b_console,
                mode_dir=result_root / mode,
            )
            completed.append(mode)

        suite_result = "passed"
        suite = {
            "started_at": stamp,
            "created_at": datetime.now().astimezone().isoformat(),
            "commit": commit,
            "dirty_worktree": dirty,
            "series_policy": "ack-gated",
            "primary_latency_definition": "QUEUE(sender) -> RX(receiver)",
            "decomposition": ["QUEUE->TX", "TX->RX"],
            "phase_randomization": "Uniform[0, mean scheduler interval), applied before QUEUE and excluded from latency",
            "phase_jitter_seed": args.phase_jitter_seed,
            "modes_requested": modes,
            "completed": completed,
            "messages": args.messages,
            "message_size": args.message_size,
            "cbr_interval_ms": args.cbr_interval_ms,
            "poisson_lambda": args.poisson_lambda,
            "result": suite_result,
        }
        (result_root / "suite_run.json").write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")

        if not args.skip_analysis and set(modes) == {"cbr", "poisson"}:
            proc = subprocess.run([sys.executable, str(ANALYZE), str(result_root)], cwd=PROJECT_ROOT)
            if proc.returncode != 0:
                raise Bt06Error(f"BT-06 analysis exited with code {proc.returncode}")

        print("\nBT-06 functional execution: PASS", flush=True)
        print("[i] PASS validates experiment integrity; latency values themselves are experimental observations.", flush=True)
        return 0
    except KeyboardInterrupt:
        suite_result = "interrupted"
        return 130
    except Exception as exc:
        warn(str(exc))
        return 1
    finally:
        if not (result_root / "suite_run.json").exists():
            suite = {
                "started_at": stamp,
                "created_at": datetime.now().astimezone().isoformat(),
                "commit": commit,
                "dirty_worktree": dirty,
                "series_policy": "ack-gated",
                "modes_requested": modes,
                "completed": completed,
                "messages": args.messages,
                "message_size": args.message_size,
                "cbr_interval_ms": args.cbr_interval_ms,
                "poisson_lambda": args.poisson_lambda,
                "result": suite_result,
            }
            (result_root / "suite_run.json").write_text(json.dumps(suite, indent=2) + "\n", encoding="utf-8")

        b5.close_console(client_a_console)
        b5.close_console(client_b_console)
        b5.close_console(server_console)
        b5.stop_process(socks_socat, "VM-to-Chutney SOCKS socat")
        b5.stop_process(service_socat, "hidden-service socat")
        b5.stop_process(control_socat, "control-port socat")
        b5.remove_firewall_rule_if_added(firewall_added)
        if not args.keep_environment:
            if chutney_started:
                b5.stop_chutney()
            b5.destroy_vms()
        else:
            info("Keeping VMs and Chutney running (--keep-environment)")
        info(f"Results directory: {result_root}")


if __name__ == "__main__":
    raise SystemExit(main())
