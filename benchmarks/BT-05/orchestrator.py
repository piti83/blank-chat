#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import os
import re
import shlex
import signal
import shutil
import socket
import struct
import subprocess
import sys
import threading
import time
import uuid
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

import pexpect


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BENCHMARKS_DIR = PROJECT_ROOT / "benchmarks"
TEST_DIR = BENCHMARKS_DIR / "BT-05"
VM_MANAGER = BENCHMARKS_DIR / "vm_manager.py"
CHUTNEY_MANAGER = BENCHMARKS_DIR / "chutney_manager.sh"
CHUTNEY_DIR = BENCHMARKS_DIR / "chutney"
CHUTNEY_BIN = CHUTNEY_DIR / "chutney"
CHUTNEY_DATA_DIR = PROJECT_ROOT / ".chutney" / "net"
ANALYZE = TEST_DIR / "analyze.py"

SERVER_VM = "bc-server"
CLIENT_A_VM = "bc-client-1"
CLIENT_B_VM = "bc-client-2"
ALL_VMS = (SERVER_VM, CLIENT_A_VM, CLIENT_B_VM)

GATEWAY_IP = "192.168.122.1"
SERVER_TOR_CONTROL_PORT = 8006
VM_TOR_SOCKS_PORT = 9050
SERVER_PORT = 8080
CHUTNEY_NODES_DIR = PROJECT_ROOT / ".chutney" / "net" / "nodes"

REMOTE_SERVER_CONFIG = "/etc/blank-chat/server_config.toml"
REMOTE_SERVER_LOG = "/tmp/bt05_server.log"
REMOTE_SERVER_PID = "/tmp/bt05_server.pid"

REMOTE_CLIENT_CONFIG = "/etc/blank-chat/client_config.toml"
REMOTE_CONTACTS = "/etc/blank-chat/contacts.json"
REMOTE_IDENTITY = "/etc/blank-chat/identity.json"
REMOTE_CLIENT_FIFO = "/tmp/bt05_client.in"
REMOTE_CLIENT_LOG = "/tmp/bt05_client.log"
REMOTE_CLIENT_PID = "/tmp/bt05_client.pid"
REMOTE_CLIENT_WORKDIR = "/tmp/bt05-client"

SHELL_PROMPT = "__BC_BT05_PROMPT__ "
FRAME_HEADER_SIZE = 21
ACTION_AUTH_CHALLENGE = 0x04
CONTROL_PAYLOAD_SIZE = 498 - FRAME_HEADER_SIZE

DEFAULT_MESSAGES = 1000
DEFAULT_MESSAGE_SIZE = 150
DEFAULT_CBR_INTERVAL_MS = 10_000
DEFAULT_POISSON_LAMBDA = 0.1
DEFAULT_PROGRESS_INTERVAL_SECONDS = 60.0
PFS_MESSAGE_INTERVAL = 0

QUEUE_RE = re.compile(r"BT05_EVENT QUEUE timestamp_ns=(\d+) msg_id=([0-9a-fA-F]+)")
RX_RE = re.compile(r"BT05_EVENT RX timestamp_ns=(\d+) msg_id=([0-9a-fA-F]+)")
ACK_RE = re.compile(r"BT05_EVENT ACK timestamp_ns=(\d+) msg_id=([0-9a-fA-F]+)")
MNEMONIC_RE = re.compile(r"\b(?:[a-z]+-){23}[a-z]+\b")
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")


class Bt05Error(RuntimeError):
    pass


@dataclass
class ModeRun:
    started_at: str
    finished_at: str = ""
    commit: str = ""
    dirty_worktree: bool = False
    mode: str = ""
    messages_requested: int = DEFAULT_MESSAGES
    message_size: int = DEFAULT_MESSAGE_SIZE
    v_user_bytes: int = DEFAULT_MESSAGES * DEFAULT_MESSAGE_SIZE
    cbr_interval_ms: int = DEFAULT_CBR_INTERVAL_MS
    poisson_lambda: float = DEFAULT_POISSON_LAMBDA
    poisson_min_clamp_ms: int = 100
    pfs_message_interval: int = PFS_MESSAGE_INTERVAL
    server_ip: str = ""
    client_a_ip: str = ""
    client_b_ip: str = ""
    onion: str = ""
    capture_interface: str = "virbr0"
    capture_filter: str = ""
    tor_client_node: str = ""
    tor_client_socks_port: int = 0
    tor_client_control_port: int = 0
    tor_or_read_bytes: int = 0
    tor_or_written_bytes: int = 0
    tor_or_event_count: int = 0
    tor_traffic_read_delta: int = 0
    tor_traffic_written_delta: int = 0
    server_edge_packets: int = 0
    server_edge_bytes: int = 0
    messages_queued: int = 0
    messages_received: int = 0
    acks_delivered: int = 0
    unique_message_ids: int = 0
    captured_packets: int = 0
    v_total_bytes: int = 0
    overhead_percent: float | None = None
    expansion_factor: float | None = None
    duration_seconds: float = 0.0
    client_a_alive_end: bool = False
    client_b_alive_end: bool = False
    server_alive_end: bool = False
    result: str = "incomplete"




@dataclass(frozen=True)
class ChutneyTorNode:
    name: str
    socks_port: int
    control_port: int


CONN_BW_RE = re.compile(
    r"^650 CONN_BW ID=(\S+) TYPE=(\S+) READ=(\d+) WRITTEN=(\d+)"
)


class TorConnBwMonitor:
    def __init__(self, control_port: int) -> None:
        self.control_port = control_port
        self.sock: socket.socket | None = None
        self.reader = None
        self.thread: threading.Thread | None = None
        self.events: list[tuple[int, str, str, int, int]] = []
        self.lock = threading.Lock()
        self.stop_event = threading.Event()
        self.error: Exception | None = None

    def _read_reply(self) -> list[str]:
        if self.reader is None:
            raise Bt05Error("Tor control reader is not initialized")
        lines: list[str] = []
        while True:
            raw = self.reader.readline()
            if not raw:
                raise Bt05Error("Tor control connection closed while waiting for reply")
            line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
            lines.append(line)
            if re.match(r"^\d{3} ", line):
                return lines

    def start(self) -> None:
        self.sock = socket.create_connection(("127.0.0.1", self.control_port), timeout=5)
        self.sock.settimeout(None)
        self.reader = self.sock.makefile("rb")
        self.sock.sendall(b"AUTHENTICATE\r\n")
        reply = self._read_reply()
        if not reply[-1].startswith("250 "):
            raise Bt05Error("Tor control AUTHENTICATE failed: " + " | ".join(reply))
        self.sock.sendall(b"SETEVENTS CONN_BW\r\n")
        reply = self._read_reply()
        if not reply[-1].startswith("250 "):
            raise Bt05Error("Tor does not support CONN_BW events: " + " | ".join(reply))
        self.thread = threading.Thread(target=self._run, name="bt05-tor-connbw", daemon=True)
        self.thread.start()

    def _run(self) -> None:
        assert self.reader is not None
        try:
            while not self.stop_event.is_set():
                raw = self.reader.readline()
                if not raw:
                    break
                line = raw.decode("utf-8", errors="replace").rstrip("\r\n")
                match = CONN_BW_RE.match(line)
                if not match:
                    continue
                conn_id, conn_type, read_b, written_b = match.groups()
                with self.lock:
                    self.events.append(
                        (time.time_ns(), conn_id, conn_type, int(read_b), int(written_b))
                    )
        except Exception as exc:
            if not self.stop_event.is_set():
                self.error = exc

    def mark(self) -> int:
        with self.lock:
            return len(self.events)

    def since(self, mark: int) -> list[tuple[int, str, str, int, int]]:
        with self.lock:
            return list(self.events[mark:])

    def stop(self) -> None:
        self.stop_event.set()
        if self.sock is not None:
            try:
                self.sock.shutdown(socket.SHUT_RDWR)
            except OSError:
                pass
            self.sock.close()
        if self.thread is not None:
            self.thread.join(timeout=2)
        if self.reader is not None:
            try:
                self.reader.close()
            except Exception:
                pass
        if self.error is not None:
            raise Bt05Error(f"Tor CONN_BW monitor failed: {self.error}")


@dataclass
class ClientHandle:
    name: str
    vm: str
    peer_alias: str
    console: pexpect.spawn


@dataclass(frozen=True)
class ClockSample:
    host_before_ns: int
    host_after_ns: int
    host_mid_ns: int
    vm_ns: int
    offset_vm_minus_host_ns: int
    rtt_ns: int


def sample_vm_clock(child: pexpect.spawn, *, timeout: float = 10.0) -> ClockSample:
    marker = "BT06_EVENT CLOCK"
    before_count = client_log_count(child, marker)

    # Trigger an in-process timestamp using the existing `list` command. The
    # timestamp is produced by the same std::chrono::system_clock used by the
    # QUEUE/RX diagnostics, so no external `date` implementation is needed.
    command = (
        "printf '%s\\n' list >&3; "
        "i=0; "
        f"while [ \"$(grep -Fc -- {shlex.quote(marker)} {REMOTE_CLIENT_LOG} 2>/dev/null || true)\" "
        f"-le {before_count} ] && [ $i -lt 200 ]; do "
        "sleep 0.01; i=$((i + 1)); done; "
        f"grep -F -- {shlex.quote(marker)} {REMOTE_CLIENT_LOG} 2>/dev/null | tail -n 1"
    )

    host_before_ns = time.time_ns()
    output, rc = console_cmd(child, command, check=False, timeout=timeout)
    host_after_ns = time.time_ns()

    if rc != 0:
        raise Bt05Error("In-process VM clock calibration command failed")

    match = re.search(r"BT06_EVENT CLOCK timestamp_ns=(\d{16,20})", output)
    if not match:
        raise Bt05Error(
            "VM clock calibration did not observe BT06_EVENT CLOCK. "
            "Rebuild the client image with './benchmarks/BT-05/run.sh --prepare ...'."
        )

    vm_ns = int(match.group(1))
    host_mid_ns = (host_before_ns + host_after_ns) // 2
    rtt_ns = host_after_ns - host_before_ns
    return ClockSample(
        host_before_ns=host_before_ns,
        host_after_ns=host_after_ns,
        host_mid_ns=host_mid_ns,
        vm_ns=vm_ns,
        offset_vm_minus_host_ns=vm_ns - host_mid_ns,
        rtt_ns=rtt_ns,
    )


def calibrate_vm_clock(
    child: pexpect.spawn, vm_name: str, *, samples: int = 9
) -> dict:
    if samples < 3:
        raise Bt05Error("Clock calibration requires at least 3 samples")

    collected: list[ClockSample] = []
    for _ in range(samples):
        collected.append(sample_vm_clock(child))
        time.sleep(0.02)

    # NTP-style midpoint estimate. The sample with the smallest measured RTT
    # is least exposed to asymmetric serial-console scheduling delay.
    selected = min(collected, key=lambda item: item.rtt_ns)
    offsets = sorted(item.offset_vm_minus_host_ns for item in collected)
    median_offset = offsets[len(offsets) // 2]

    result = {
        "vm": vm_name,
        "method": "host midpoint around in-process BT06_EVENT CLOCK; minimum-RTT sample selected",
        "selected": asdict(selected),
        "median_offset_vm_minus_host_ns": median_offset,
        "samples": [asdict(item) for item in collected],
    }
    ok(
        f"Clock calibration {vm_name}: offset={selected.offset_vm_minus_host_ns / 1e6:.3f} ms, "
        f"RTT={selected.rtt_ns / 1e6:.3f} ms"
    )
    return result


def info(message: str) -> None:
    print(f"[i] {message}", flush=True)


def ok(message: str) -> None:
    print(f"[+] {message}", flush=True)


def warn(message: str) -> None:
    print(f"[!] {message}", file=sys.stderr, flush=True)


def run(
    cmd: list[str],
    *,
    check: bool = True,
    capture: bool = False,
) -> subprocess.CompletedProcess[str]:
    info("$ " + shlex.join(cmd))
    result = subprocess.run(
        cmd,
        cwd=str(PROJECT_ROOT),
        text=True,
        capture_output=capture,
    )
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip() if capture else ""
        raise Bt05Error(
            f"Command failed with exit code {result.returncode}: {shlex.join(cmd)}"
            + (f"\n{detail}" if detail else "")
        )
    return result


def require_dependencies() -> None:
    required = (
        "virsh",
        "virt-install",
        "qemu-img",
        "socat",
        "sudo",
        "iptables",
        "pkill",
        "tshark",
    )
    missing = [name for name in required if shutil.which(name) is None]
    if missing:
        raise Bt05Error("Missing host dependencies: " + ", ".join(missing))

    if shutil.which("tcpdump") is None and shutil.which("tshark") is None:
        raise Bt05Error("BT-05 requires tcpdump or tshark for PCAP capture")

    for path in (VM_MANAGER, CHUTNEY_MANAGER, ANALYZE):
        if not path.exists():
            raise Bt05Error(f"Required file does not exist: {path}")


def acquire_sudo() -> None:
    info("Acquiring sudo credentials...")
    run(["sudo", "-v"])


def repair_workspace_permissions() -> None:
    import grp
    import os
    import pwd

    uid = os.getuid()
    gid = os.getgid()
    owner = f"{pwd.getpwuid(uid).pw_name}:{grp.getgrgid(gid).gr_name}"

    for path in (PROJECT_ROOT / ".chutney", BENCHMARKS_DIR / "results"):
        if not path.exists():
            continue

        needs_fix = False
        for candidate in [path, *path.rglob("*")]:
            try:
                stat = candidate.stat()
            except FileNotFoundError:
                continue
            if stat.st_uid != uid or stat.st_gid != gid:
                needs_fix = True
                break

        if needs_fix:
            info(f"Repairing ownership of {path}...")
            run(["sudo", "chown", "-R", owner, str(path)])


def cleanup_stale_chutney_tor() -> None:
    runtime = str((PROJECT_ROOT / ".chutney" / "net").resolve())
    result = subprocess.run(
        ["ps", "-eo", "pid=,comm=,args="],
        text=True,
        capture_output=True,
        check=True,
    )

    pids: list[int] = []
    for line in result.stdout.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and parts[1] == "tor" and runtime in parts[2]:
            try:
                pids.append(int(parts[0]))
            except ValueError:
                pass

    if pids:
        run(["sudo", "kill", "-TERM", *map(str, pids)], check=False)
        time.sleep(1)


def recreate_vms() -> None:
    info("Recreating benchmark VMs from Yocto images...")
    run([sys.executable, str(VM_MANAGER)])


def get_vm_ip_once(vm_name: str) -> str | None:
    result = run(
        ["virsh", "-c", "qemu:///system", "domifaddr", vm_name],
        check=False,
        capture=True,
    )
    match = re.search(r"(\d+\.\d+\.\d+\.\d+)/\d+", result.stdout)
    if match:
        return match.group(1)

    result = run(
        ["virsh", "-c", "qemu:///system", "domiflist", vm_name],
        check=False,
        capture=True,
    )
    mac_match = re.search(r"\b([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})\b", result.stdout)
    if not mac_match:
        return None

    mac = mac_match.group(1).lower()
    leases = run(
        ["virsh", "-c", "qemu:///system", "net-dhcp-leases", "default"],
        check=False,
        capture=True,
    )
    for line in leases.stdout.splitlines():
        if mac in line.lower():
            ip_match = re.search(r"(\d+\.\d+\.\d+\.\d+)/\d+", line)
            if ip_match:
                return ip_match.group(1)
    return None


def wait_for_vm_ip(vm_name: str, timeout: float = 300.0) -> str:
    info(f"Waiting for {vm_name} to obtain DHCP...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ip = get_vm_ip_once(vm_name)
        if ip:
            ok(f"{vm_name} IP: {ip}")
            return ip
        time.sleep(2)
    raise Bt05Error(f"Timed out waiting for IP address of {vm_name}")


def run_chutney_launch_phase(phase: int, action: str, *, check: bool = True) -> subprocess.CompletedProcess[str]:
    env = os.environ.copy()
    env["CHUTNEY_DATA_DIR"] = str(CHUTNEY_DATA_DIR)
    env["CHUTNEY_LISTEN_ADDRESS"] = "127.0.0.1"
    env["CHUTNEY_DNS_CONF"] = "/dev/null"
    env["CHUTNEY_LAUNCH_PHASE"] = str(phase)

    cmd = [str(CHUTNEY_BIN), action]
    info(f"$ CHUTNEY_LAUNCH_PHASE={phase} " + shlex.join(cmd))
    result = subprocess.run(
        cmd,
        cwd=str(CHUTNEY_DIR),
        env=env,
        text=True,
    )
    if check and result.returncode != 0:
        raise Bt05Error(
            f"Chutney launch phase {phase} action '{action}' failed "
            f"with exit code {result.returncode}"
        )
    return result


def start_chutney() -> None:
    # The shared manager intentionally starts launch phase 1 only.  In
    # hs-v3-min that is the four authorities plus three relays (7 nodes).
    # The dedicated client node (*c) lives in launch phase 2 together with
    # the static HS node, so BT-05 explicitly starts that phase as well.
    run(["bash", str(CHUTNEY_MANAGER), "clean"], check=False)
    run(["bash", str(CHUTNEY_MANAGER), "start"])
    run(["bash", str(CHUTNEY_MANAGER), "status"])

    info("Starting Chutney launch phase 2 for the dedicated client Tor node...")
    run_chutney_launch_phase(2, "start")
    run_chutney_launch_phase(2, "wait_for_bootstrap")
    run_chutney_launch_phase(2, "status")


def stop_chutney() -> None:
    if not CHUTNEY_MANAGER.exists():
        return
    run(["bash", str(CHUTNEY_MANAGER), "stop"], check=False)


def parse_tor_port(torrc: Path, key: str) -> int:
    pattern = re.compile(rf"^\s*{re.escape(key)}\s+(?:127\.0\.0\.1:)?(\d+)\s*$")
    for raw in torrc.read_text(encoding="utf-8", errors="replace").splitlines():
        match = pattern.match(raw)
        if match and int(match.group(1)) > 0:
            return int(match.group(1))
    raise Bt05Error(f"Could not find non-zero {key} in {torrc}")


def discover_dedicated_client_tor() -> ChutneyTorNode:
    if not CHUTNEY_NODES_DIR.exists():
        raise Bt05Error(f"Chutney nodes directory does not exist: {CHUTNEY_NODES_DIR}")
    candidates = sorted(
        path for path in CHUTNEY_NODES_DIR.iterdir()
        if path.is_dir() and path.name.endswith("c")
    )
    if len(candidates) != 1:
        names = ", ".join(path.name for path in candidates) or "none"
        raise Bt05Error(
            "Expected exactly one dedicated Chutney client node (*c), found: " + names
        )
    node_dir = candidates[0]
    torrc = node_dir / "torrc"
    node = ChutneyTorNode(
        name=node_dir.name,
        socks_port=parse_tor_port(torrc, "SocksPort"),
        control_port=parse_tor_port(torrc, "ControlPort"),
    )
    ok(
        f"Dedicated Chutney client Tor: {node.name} "
        f"(SOCKS={node.socks_port}, Control={node.control_port})"
    )
    return node


def check_socks5(port: int) -> None:
    info(f"Waiting for dedicated Chutney client SOCKS5 on 127.0.0.1:{port}...")
    deadline = time.monotonic() + 60
    while time.monotonic() < deadline:
        try:
            with socket.create_connection(("127.0.0.1", port), timeout=1):
                ok("Dedicated Chutney client SOCKS5 is ready")
                return
        except OSError:
            time.sleep(1)
    raise Bt05Error("Dedicated Chutney client SOCKS5 did not become ready")


def tor_getinfo_traffic(control_port: int) -> tuple[int, int]:
    with socket.create_connection(("127.0.0.1", control_port), timeout=5) as sock:
        reader = sock.makefile("rb")
        try:
            sock.sendall(b"AUTHENTICATE\r\n")
            auth = reader.readline().decode("utf-8", errors="replace").strip()
            if not auth.startswith("250"):
                raise Bt05Error(f"Tor control AUTHENTICATE failed: {auth}")
            sock.sendall(b"GETINFO traffic/read traffic/written\r\n")
            values: dict[str, int] = {}
            while True:
                raw = reader.readline()
                if not raw:
                    raise Bt05Error("Tor control connection closed during GETINFO")
                line = raw.decode("utf-8", errors="replace").strip()
                if line.startswith("250-traffic/read="):
                    values["read"] = int(line.split("=", 1)[1])
                elif line.startswith("250-traffic/written="):
                    values["written"] = int(line.split("=", 1)[1])
                elif line.startswith("250 "):
                    break
                elif line.startswith(("5", "4")):
                    raise Bt05Error(f"Tor GETINFO traffic failed: {line}")
            if "read" not in values or "written" not in values:
                raise Bt05Error("Tor GETINFO traffic did not return both counters")
            return values["read"], values["written"]
        finally:
            reader.close()


def ensure_firewall_rule() -> bool:
    check = subprocess.run(
        ["sudo", "iptables", "-C", "INPUT", "-i", "virbr0", "-j", "ACCEPT"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if check.returncode == 0:
        return False
    run(["sudo", "iptables", "-I", "INPUT", "-i", "virbr0", "-j", "ACCEPT"])
    return True


def remove_firewall_rule_if_added(added: bool) -> None:
    if added:
        subprocess.run(
            ["sudo", "iptables", "-D", "INPUT", "-i", "virbr0", "-j", "ACCEPT"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def stop_stale_socat() -> None:
    for port in (SERVER_TOR_CONTROL_PORT, SERVER_PORT, VM_TOR_SOCKS_PORT):
        subprocess.run(
            ["sudo", "pkill", "-f", f"socat.*TCP-LISTEN:{port}"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def start_socat(args: list[str], log_path: Path) -> subprocess.Popen[str]:
    handle = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        ["socat", *args],
        cwd=str(PROJECT_ROOT),
        text=True,
        stdout=handle,
        stderr=subprocess.STDOUT,
    )
    handle.close()
    time.sleep(0.5)
    if proc.poll() is not None:
        detail = log_path.read_text(encoding="utf-8", errors="replace").strip()
        raise Bt05Error(f"socat exited immediately: {' '.join(args)}\n{detail}")
    ok("Started socat: " + " ".join(args))
    return proc


def stop_process(process: subprocess.Popen | None, name: str) -> None:
    if process is None or process.poll() is not None:
        return
    info(f"Stopping {name}...")
    process.terminate()
    try:
        process.wait(timeout=3)
    except subprocess.TimeoutExpired:
        process.kill()
        process.wait(timeout=2)


def login_to_console(vm_name: str, timeout: float = 180.0) -> pexpect.spawn:
    info(f"Opening serial console for {vm_name}...")
    child = pexpect.spawn(
        "virsh",
        ["-c", "qemu:///system", "console", vm_name, "--force"],
        encoding="utf-8",
        timeout=timeout,
    )

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        index = child.expect(
            [r"login:\s*$", r"#\s*$", r"Press Enter", pexpect.TIMEOUT, pexpect.EOF],
            timeout=5,
        )
        if index == 0:
            child.sendline("root")
            child.expect(r"#\s*$", timeout=20)
            break
        if index == 1:
            break
        if index == 2:
            child.sendline("")
            continue
        if index == 3:
            child.sendline("")
            continue
        raise Bt05Error(f"Console for {vm_name} closed unexpectedly")
    else:
        raise Bt05Error(f"Timed out logging into {vm_name}")

    child.sendline(f"export PS1='{SHELL_PROMPT}'")
    child.expect(re.escape(SHELL_PROMPT), timeout=10)
    child.sendline("stty -echo")
    child.expect(re.escape(SHELL_PROMPT), timeout=10)
    ok(f"Logged into {vm_name} as root")
    return child


def console_cmd(
    child: pexpect.spawn,
    command: str,
    *,
    timeout: float = 60.0,
    check: bool = True,
) -> tuple[str, int]:
    token = uuid.uuid4().hex
    begin_marker = f"__BC_BEGIN_{token}__"
    end_marker = f"__BC_END_{token}__"
    wrapped = (
        f"printf '\\n{begin_marker}\\n'; "
        f"{command}; __bc_rc=$?; "
        f"printf '\\n{end_marker}:%s\\n' \"$__bc_rc\""
    )
    child.send(wrapped + "\r\n")
    child.expect(re.escape(begin_marker), timeout=timeout)
    child.expect(re.escape(end_marker) + r":(\d+)", timeout=timeout)
    output = child.before.replace("\r", "").strip()
    rc = int(child.match.group(1))
    child.expect(re.escape(SHELL_PROMPT), timeout=10)

    if check and rc != 0:
        raise Bt05Error(f"Command failed inside VM (rc={rc}): {command}\n{output}")
    return output, rc


def upload_text(child: pexpect.spawn, remote_path: str, text: str) -> None:
    quoted = shlex.quote(remote_path)
    console_cmd(child, f": > {quoted}")
    for line in text.splitlines():
        console_cmd(child, f"printf '%s\\n' {shlex.quote(line)} >> {quoted}")


def remote_cat(child: pexpect.spawn, path: str) -> str:
    output, _ = console_cmd(
        child,
        f"cat {shlex.quote(path)} 2>/dev/null || true",
        timeout=90,
        check=False,
    )
    return output


def configure_server(child: pexpect.spawn) -> None:
    config = '''[network]
listen_host = "0.0.0.0"
listen_port = 8080
tor_control_host = "192.168.122.1"
tor_control_port = 8006

[security]
memory_quota_percent = 80
max_messages_per_mailbox = 5000000
'''
    console_cmd(child, "mkdir -p /etc/blank-chat")
    upload_text(child, REMOTE_SERVER_CONFIG, config)


def start_server(child: pexpect.spawn) -> None:
    console_cmd(child, "killall blank_chat_server 2>/dev/null || true", check=False)
    console_cmd(child, f"rm -f {REMOTE_SERVER_LOG} {REMOTE_SERVER_PID}")
    console_cmd(
        child,
        "mkdir -p /etc/blank-chat/logs && cd /etc/blank-chat && "
        f"blank_chat_server > {REMOTE_SERVER_LOG} 2>&1 & echo $! > {REMOTE_SERVER_PID}",
    )
    time.sleep(1)


def stop_server(child: pexpect.spawn) -> None:
    console_cmd(
        child,
        f"kill $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null || true",
        check=False,
    )
    time.sleep(0.5)


def wait_for_onion(child: pexpect.spawn, timeout: float = 60.0) -> str:
    deadline = time.monotonic() + timeout
    onion_re = re.compile(r"\b([a-z2-7]{56})(?:\.onion)?\b")
    while time.monotonic() < deadline:
        output, _ = console_cmd(
            child,
            f"grep -Eo '[a-z2-7]{{56}}(\\.onion)?' {REMOTE_SERVER_LOG} "
            "2>/dev/null | tail -n 1 || true",
            check=False,
        )
        match = onion_re.search(output)
        if match:
            onion = match.group(1) + ".onion"
            ok(f"Hidden service: {onion}")
            return onion
        time.sleep(1)
    raise Bt05Error("Timed out waiting for hidden-service address")


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data.extend(chunk)
    return bytes(data)


def socks5_connect(onion: str, socks_port: int, target_port: int = 80, timeout: float = 5.0) -> socket.socket:
    domain = onion.encode("ascii")
    sock = socket.create_connection(("127.0.0.1", socks_port), timeout=timeout)
    sock.settimeout(timeout)
    try:
        sock.sendall(b"\x05\x01\x00")
        if recv_exact(sock, 2) != b"\x05\x00":
            raise ConnectionError("SOCKS no-auth rejected")
        request = (
            b"\x05\x01\x00\x03"
            + bytes([len(domain)])
            + domain
            + struct.pack(">H", target_port)
        )
        sock.sendall(request)
        header = recv_exact(sock, 4)
        if header[1] != 0:
            raise ConnectionError(f"SOCKS CONNECT failed with REP=0x{header[1]:02x}")
        atyp = header[3]
        if atyp == 1:
            recv_exact(sock, 4)
        elif atyp == 3:
            recv_exact(sock, recv_exact(sock, 1)[0])
        elif atyp == 4:
            recv_exact(sock, 16)
        else:
            raise ConnectionError("Unsupported SOCKS ATYP")
        recv_exact(sock, 2)
        return sock
    except Exception:
        sock.close()
        raise


def wait_for_hidden_service(onion: str, socks_port: int, timeout: float = 60.0) -> None:
    info("Waiting for hidden service through Chutney...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sock = socks5_connect(onion, socks_port=socks_port, timeout=5)
            try:
                header = recv_exact(sock, FRAME_HEADER_SIZE)
                length = struct.unpack("<I", header[17:21])[0]
                payload = recv_exact(sock, length) if length else b""
                if header[0] == ACTION_AUTH_CHALLENGE and len(payload) == CONTROL_PAYLOAD_SIZE:
                    ok("Hidden service reachable")
                    return
            finally:
                sock.close()
        except Exception:
            pass
        time.sleep(2)
    raise Bt05Error("Hidden service did not become reachable")


def configure_client(
    child: pexpect.spawn,
    onion: str,
    mode: str,
    cbr_interval_ms: int,
    poisson_lambda: float,
) -> None:
    config = f'''[network]
tor_socks_host = "{GATEWAY_IP}"
tor_socks_port = {VM_TOR_SOCKS_PORT}

[relay]
onion_address = "{onion}"
onion_port = 80

[obfuscation]
mode = "{mode}"
cbr_interval_ms = {cbr_interval_ms}
poisson_lambda = {poisson_lambda}

[storage]
contacts_file_path = "{REMOTE_CONTACTS}"

[security]
pfs_message_interval = {PFS_MESSAGE_INTERVAL}
'''
    console_cmd(child, "mkdir -p /etc/blank-chat")
    upload_text(child, REMOTE_CLIENT_CONFIG, config)


def assert_client_binary_has_diagnostics(child: pexpect.spawn, vm_name: str) -> None:
    markers = ("BT05_EVENT QUEUE", "BT05_EVENT RX", "BT05_EVENT ACK", "BT06_EVENT CLOCK")
    for marker in markers:
        _, rc = console_cmd(
            child,
            f"grep -q {shlex.quote(marker)} /usr/bin/blank_chat_client 2>/dev/null",
            check=False,
        )
        if rc != 0:
            raise Bt05Error(
                f"{vm_name} does not contain BT-05/BT-06 diagnostics. Run "
                "'./benchmarks/BT-05/run.sh --prepare --messages 20 "
                "--cbr-interval-ms 1000 --poisson-lambda 1.0' first."
            )


def strip_ansi(text: str) -> str:
    return ANSI_RE.sub("", text)


def client_command(child: pexpect.spawn, command: str) -> None:
    console_cmd(child, f"printf '%s\\n' {shlex.quote(command)} >&3")


def client_log_count(child: pexpect.spawn, pattern: str) -> int:
    output, _ = console_cmd(
        child,
        f"grep -Fc -- {shlex.quote(pattern)} {REMOTE_CLIENT_LOG} 2>/dev/null || true",
        check=False,
    )
    try:
        return int(output.splitlines()[-1].strip())
    except (ValueError, IndexError):
        return 0


def wait_client_log_count(
    child: pexpect.spawn,
    pattern: str,
    minimum: int,
    timeout: float = 60.0,
) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if client_log_count(child, pattern) >= minimum:
            return
        time.sleep(0.25)
    raise Bt05Error(f"Timed out waiting for client log: {pattern}")


def wait_client_log(child: pexpect.spawn, pattern: str, timeout: float = 60.0) -> None:
    wait_client_log_count(child, pattern, 1, timeout)


def start_client(child: pexpect.spawn, vm_name: str) -> None:
    info(f"Starting blank_chat_client on {vm_name}...")
    console_cmd(
        child,
        f"kill $(cat {REMOTE_CLIENT_PID} 2>/dev/null) 2>/dev/null || true",
        check=False,
    )
    console_cmd(
        child,
        f"rm -f {REMOTE_IDENTITY} {REMOTE_CONTACTS} {REMOTE_CLIENT_FIFO} "
        f"{REMOTE_CLIENT_LOG} {REMOTE_CLIENT_PID}",
    )
    console_cmd(
        child,
        f"rm -rf {REMOTE_CLIENT_WORKDIR} && mkdir -p {REMOTE_CLIENT_WORKDIR}/logs && "
        f"mkfifo {REMOTE_CLIENT_FIFO} && exec 3<> {REMOTE_CLIENT_FIFO}",
    )
    console_cmd(
        child,
        f"cd {REMOTE_CLIENT_WORKDIR} && blank_chat_client <&3 > {REMOTE_CLIENT_LOG} 2>&1 & "
        f"echo $! > {REMOTE_CLIENT_PID}",
    )
    wait_client_log(child, "Do you want to generate a new Identity Key now?", 20)
    client_command(child, "y")
    wait_client_log(child, "New identity generated and saved successfully.", 20)
    wait_client_log(child, "--- Blank Chat ---", 20)


def stop_client(child: pexpect.spawn) -> None:
    try:
        client_command(child, "exit")
        time.sleep(0.3)
    except Exception:
        pass
    console_cmd(
        child,
        f"kill $(cat {REMOTE_CLIENT_PID} 2>/dev/null) 2>/dev/null || true",
        check=False,
    )
    console_cmd(child, "exec 3>&- 2>/dev/null || true", check=False)


def client_alive(child: pexpect.spawn) -> bool:
    _, rc = console_cmd(
        child,
        f"kill -0 $(cat {REMOTE_CLIENT_PID} 2>/dev/null) 2>/dev/null",
        check=False,
    )
    return rc == 0


def server_alive(child: pexpect.spawn) -> bool:
    _, rc = console_cmd(
        child,
        f"kill -0 $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null",
        check=False,
    )
    return rc == 0


def extract_mnemonic(child: pexpect.spawn) -> str:
    before = client_log_count(child, "Your Identity Key (BIP39 Mnemonic)")
    client_command(child, "mykey")
    wait_client_log_count(child, "Your Identity Key (BIP39 Mnemonic)", before + 1, 10)
    output, _ = console_cmd(child, f"tail -n 100 {REMOTE_CLIENT_LOG}", check=False)
    matches = MNEMONIC_RE.findall(strip_ansi(output))
    if not matches:
        raise Bt05Error("Could not parse BIP39 mnemonic")
    return matches[-1]


def add_contact(child: pexpect.spawn, alias: str, mnemonic: str) -> None:
    marker = f"Contact '{alias}' added successfully."
    before = client_log_count(child, marker)
    client_command(child, f"add {alias} {mnemonic}")
    wait_client_log_count(child, marker, before + 1, 15)


def connect_client(child: pexpect.spawn, mode: str) -> None:
    before = client_log_count(child, "Successfully connected.")
    client_command(child, "connect")
    wait_client_log_count(child, "Successfully connected.", before + 1, 60)
    expected = (
        "Initializing Constant Bit Rate"
        if mode == "cbr"
        else "Initializing Stochastic Poisson Obfuscator"
    )
    wait_client_log(child, expected, 20)


def parse_initial_pfs(raw: str, alias: str) -> bool:
    try:
        parsed = json.loads(raw)
    except json.JSONDecodeError:
        return False
    contacts = parsed.get("contacts")
    if not isinstance(contacts, list):
        return False
    for item in contacts:
        if item.get("alias") == alias:
            return bool(item.get("initialPfsComplete", False))
    return False


def wait_for_initial_pfs(a: ClientHandle, b: ClientHandle, timeout: float = 240.0) -> None:
    info("Waiting for initial PFS to complete on both clients...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        a_raw = remote_cat(a.console, REMOTE_CONTACTS)
        b_raw = remote_cat(b.console, REMOTE_CONTACTS)
        if parse_initial_pfs(a_raw, a.peer_alias) and parse_initial_pfs(b_raw, b.peer_alias):
            ok("Initial PFS completed on both clients")
            return
        if not client_alive(a.console) or not client_alive(b.console):
            raise Bt05Error("A client exited while waiting for initial PFS")
        time.sleep(1)
    raise Bt05Error("Initial PFS did not complete within timeout")


def start_server_edge_capture(server_ip: str, path: Path) -> tuple[subprocess.Popen[str], Path]:
    capture_filter = f"tcp and host {server_ip} and port {SERVER_PORT}"
    log_path = path.with_suffix(".capture.log")

    if shutil.which("tcpdump"):
        cmd = [
            "sudo",
            "tcpdump",
            "-i",
            "virbr0",
            "-nn",
            "-s",
            "0",
            "-w",
            str(path),
            "tcp",
            "and",
            "host",
            server_ip,
            "and",
            "port",
            str(SERVER_PORT),
        ]
    else:
        cmd = [
            "sudo",
            "tshark",
            "-i",
            "virbr0",
            "-f",
            capture_filter,
            "-w",
            str(path),
        ]

    info("Starting supplementary server-edge PCAP capture: " + shlex.join(cmd))
    log_file = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        cmd,
        stdout=log_file,
        stderr=subprocess.STDOUT,
        text=True,
    )
    log_file.close()
    time.sleep(1)

    if proc.poll() is not None:
        detail = log_path.read_text(encoding="utf-8", errors="replace").strip()
        raise Bt05Error(
            "PCAP capture process exited immediately"
            + (f":\n{detail}" if detail else "")
        )

    return proc, log_path


def stop_capture(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=10)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)


def queue_messages(client: ClientHandle, count: int, size: int) -> None:
    if count > 999999:
        raise Bt05Error("BT-05 message count cannot exceed 999999 with the current sequence format")

    prefix = "BT05_MSG_"
    sequence_width = 6
    separator = "_"
    filler_length = size - len(prefix) - sequence_width - len(separator)
    if filler_length < 1:
        raise Bt05Error("Message size is too small for BT-05 sequence prefix")

    filler = "X" * filler_length
    format_string = f"send {client.peer_alias} {prefix}%06d_{filler}\\n"

    before = client_log_count(client.console, "BT05_EVENT QUEUE")
    info(f"Queuing {count} unique messages of exactly {size} B on {client.vm}...")

    console_cmd(
        client.console,
        "i=1; "
        f"while [ \"$i\" -le {count} ]; do "
        f"printf {shlex.quote(format_string)} \"$i\" >&3; "
        "i=$((i+1)); "
        "done",
        timeout=max(300.0, count * 2.0),
    )

    wait_client_log_count(
        client.console,
        "BT05_EVENT QUEUE",
        before + count,
        timeout=max(300.0, count * 2.0),
    )
    ok(f"Queued all {count} benchmark messages")


def diagnostic_events(
    child: pexpect.spawn,
    marker: str,
    regex: re.Pattern[str],
) -> list[tuple[int, str]]:
    output, _ = console_cmd(
        child,
        f"grep {shlex.quote(marker)} {REMOTE_CLIENT_LOG} 2>/dev/null || true",
        timeout=120,
        check=False,
    )
    events: list[tuple[int, str]] = []
    for line in strip_ansi(output).splitlines():
        match = regex.search(line)
        if match:
            events.append((int(match.group(1)), match.group(2).lower()))
    return events


def auto_timeout_seconds(
    messages: int,
    mode: str,
    cbr_interval_ms: int,
    poisson_lambda: float,
) -> float:
    if mode == "cbr":
        mean_tick = cbr_interval_ms / 1000.0
    else:
        mean_tick = 1.0 / poisson_lambda

    # Receiver normally alternates between POLL and the ACK queued by the
    # previously received message. Therefore roughly two scheduler ticks are
    # needed per delivered message. Keep a large margin for Tor/VM jitter and
    # the stochastic Poisson tail.
    expected = 2.0 * messages * mean_tick
    return max(600.0, expected * 1.5 + 600.0)


def wait_for_series_completion(
    sender: ClientHandle,
    receiver: ClientHandle,
    expected: int,
    timeout: float,
    progress_interval: float,
) -> tuple[int, int]:
    info(
        f"Waiting for {expected} RX events and {expected} end-to-end ACKs "
        f"(timeout {timeout / 3600.0:.2f} h)..."
    )
    deadline = time.monotonic() + timeout
    next_progress = 0.0
    last_rx = -1
    last_ack = -1

    while time.monotonic() < deadline:
        now = time.monotonic()
        rx_count = client_log_count(receiver.console, "BT05_EVENT RX")
        ack_count = client_log_count(sender.console, "BT05_EVENT ACK")

        if rx_count >= expected and ack_count >= expected:
            ok(f"Series complete: RX={rx_count}/{expected}, ACK={ack_count}/{expected}")
            return rx_count, ack_count

        if not client_alive(sender.console) or not client_alive(receiver.console):
            raise Bt05Error("A client exited while the BT-05 series was in progress")

        if now >= next_progress or rx_count != last_rx or ack_count != last_ack:
            info(f"Progress: RX={rx_count}/{expected}, ACK={ack_count}/{expected}")
            last_rx = rx_count
            last_ack = ack_count
            next_progress = now + progress_interval

        time.sleep(min(30.0, max(1.0, progress_interval)))

    raise Bt05Error(
        f"Timed out waiting for series completion: "
        f"RX={client_log_count(receiver.console, 'BT05_EVENT RX')}/{expected}, "
        f"ACK={client_log_count(sender.console, 'BT05_EVENT ACK')}/{expected}"
    )


def validate_message_identity_sets(
    sender: ClientHandle,
    receiver: ClientHandle,
    expected: int,
) -> tuple[list[tuple[int, str]], list[tuple[int, str]], list[tuple[int, str]]]:
    queued = diagnostic_events(sender.console, "BT05_EVENT QUEUE", QUEUE_RE)
    received = diagnostic_events(receiver.console, "BT05_EVENT RX", RX_RE)
    acked = diagnostic_events(sender.console, "BT05_EVENT ACK", ACK_RE)

    queued_ids = [msg_id for _, msg_id in queued]
    received_ids = [msg_id for _, msg_id in received]
    acked_ids = [msg_id for _, msg_id in acked]

    if len(queued_ids) != expected:
        raise Bt05Error(f"Expected {expected} queued message IDs, got {len(queued_ids)}")
    if len(received_ids) != expected:
        raise Bt05Error(f"Expected {expected} received message IDs, got {len(received_ids)}")
    if len(acked_ids) != expected:
        raise Bt05Error(f"Expected {expected} ACKed message IDs, got {len(acked_ids)}")

    if len(set(queued_ids)) != expected:
        raise Bt05Error("Queued BT-05 message IDs are not unique")
    if set(queued_ids) != set(received_ids):
        raise Bt05Error("Received message-ID set differs from queued message-ID set")
    if set(queued_ids) != set(acked_ids):
        raise Bt05Error("ACKed message-ID set differs from queued message-ID set")

    ok("All queued, received and ACKed message-ID sets agree")
    return queued, received, acked


def pcap_wire_volume(path: Path) -> tuple[int, int]:
    if not path.exists() or path.stat().st_size == 0:
        raise Bt05Error(f"Missing or empty PCAP: {path}")

    proc = subprocess.Popen(
        ["tshark", "-r", str(path), "-T", "fields", "-e", "frame.len"],
        cwd=str(PROJECT_ROOT),
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )

    total_bytes = 0
    packet_count = 0
    assert proc.stdout is not None
    for raw_line in proc.stdout:
        value = raw_line.strip()
        if not value:
            continue
        try:
            total_bytes += int(value)
            packet_count += 1
        except ValueError as exc:
            proc.kill()
            raise Bt05Error(f"Unexpected frame.len from tshark: {value!r}") from exc

    stderr = proc.stderr.read() if proc.stderr is not None else ""
    return_code = proc.wait()
    if return_code != 0:
        raise Bt05Error(f"tshark failed while reading {path}: {stderr.strip()}")
    if packet_count == 0:
        raise Bt05Error(f"PCAP contains no packets for the measured server flow: {path}")

    return packet_count, total_bytes


def write_tor_conn_bw(
    path: Path, events: list[tuple[int, str, str, int, int]]
) -> tuple[int, int, int]:
    import csv

    or_events = [event for event in events if event[2] == "OR"]
    if not or_events:
        raise Bt05Error("No Tor CONN_BW TYPE=OR events were captured")

    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["timestamp_ns", "conn_id", "type", "read_bytes", "written_bytes"])
        for row in or_events:
            writer.writerow(row)

    read_bytes = sum(event[3] for event in or_events)
    written_bytes = sum(event[4] for event in or_events)
    return len(or_events), read_bytes, written_bytes


def collect_client_log(child: pexpect.spawn, destination: Path) -> None:
    text = remote_cat(child, REMOTE_CLIENT_LOG)
    destination.write_text(
        strip_ansi(text) + ("\n" if text and not text.endswith("\n") else ""),
        encoding="utf-8",
    )


def write_event_snapshot(
    path: Path,
    queued: list[tuple[int, str]],
    received: list[tuple[int, str]],
    acked: list[tuple[int, str]],
) -> None:
    payload = {
        "queue": [{"timestamp_ns": ts, "msg_id": msg_id} for ts, msg_id in queued],
        "rx": [{"timestamp_ns": ts, "msg_id": msg_id} for ts, msg_id in received],
        "ack": [{"timestamp_ns": ts, "msg_id": msg_id} for ts, msg_id in acked],
        "note": (
            "QUEUE and RX timestamps are retained for BT-06 end-to-end latency analysis. "
            "They are VM system_clock timestamps and must be corrected with clock_calibration.json. "
            "BT-05 itself uses message IDs only for delivery/ACK validation."
        ),
    }
    path.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")


def run_mode(
    *,
    mode: str,
    messages: int,
    message_size: int,
    cbr_interval_ms: int,
    poisson_lambda: float,
    timeout_seconds: float | None,
    progress_interval_seconds: float,
    commit: str,
    dirty: bool,
    server_ip: str,
    client_a_ip: str,
    client_b_ip: str,
    client_tor: ChutneyTorNode,
    server_console: pexpect.spawn,
    client_a_console: pexpect.spawn,
    client_b_console: pexpect.spawn,
    mode_dir: Path,
) -> None:
    mode_dir.mkdir(parents=True, exist_ok=True)
    info(f"=== BT-05 mode: {mode} ({messages} x {message_size} B) ===")

    capture_filter = f"tcp and host {server_ip} and port {SERVER_PORT}"
    run_meta = ModeRun(
        started_at=datetime.now().astimezone().isoformat(),
        commit=commit,
        dirty_worktree=dirty,
        mode=mode,
        messages_requested=messages,
        message_size=message_size,
        v_user_bytes=messages * message_size,
        cbr_interval_ms=cbr_interval_ms,
        poisson_lambda=poisson_lambda,
        server_ip=server_ip,
        client_a_ip=client_a_ip,
        client_b_ip=client_b_ip,
        capture_filter=capture_filter,
        tor_client_node=client_tor.name,
        tor_client_socks_port=client_tor.socks_port,
        tor_client_control_port=client_tor.control_port,
    )

    capture: subprocess.Popen[str] | None = None
    tor_monitor: TorConnBwMonitor | None = None
    tor_mark = 0
    tor_traffic_before: tuple[int, int] | None = None
    client_a = ClientHandle("A", CLIENT_A_VM, "bob", client_a_console)
    client_b = ClientHandle("B", CLIENT_B_VM, "alice", client_b_console)
    measurement_started: float | None = None
    clock_calibration: dict = {
        "method": (
            "Each endpoint is sampled against the single host wall clock before and after "
            "the measured series. BT-06 linearly interpolates VM-minus-host offset between "
            "the selected minimum-RTT calibration samples."
        ),
        "samples_per_point": 9,
        "pre": {},
        "post": {},
    }

    try:
        configure_server(server_console)
        start_server(server_console)
        onion = wait_for_onion(server_console)
        wait_for_hidden_service(onion, client_tor.socks_port)
        run_meta.onion = onion

        configure_client(client_a_console, onion, mode, cbr_interval_ms, poisson_lambda)
        configure_client(client_b_console, onion, mode, cbr_interval_ms, poisson_lambda)
        start_client(client_a_console, CLIENT_A_VM)
        start_client(client_b_console, CLIENT_B_VM)

        mnemonic_a = extract_mnemonic(client_a_console)
        mnemonic_b = extract_mnemonic(client_b_console)
        add_contact(client_a_console, "bob", mnemonic_b)
        add_contact(client_b_console, "alice", mnemonic_a)
        connect_client(client_a_console, mode)
        connect_client(client_b_console, mode)
        wait_for_initial_pfs(client_a, client_b)

        info("Calibrating VM clocks against the host before the measured series...")
        clock_calibration["pre"]["client_a"] = calibrate_vm_clock(
            client_a_console, CLIENT_A_VM
        )
        clock_calibration["pre"]["client_b"] = calibrate_vm_clock(
            client_b_console, CLIENT_B_VM
        )

        if client_log_count(client_a_console, "BT05_EVENT QUEUE") != 0:
            raise Bt05Error("Unexpected BT-05 queue events before measurement")
        if client_log_count(client_b_console, "BT05_EVENT RX") != 0:
            raise Bt05Error("Unexpected BT-05 RX events before measurement")
        if client_log_count(client_a_console, "BT05_EVENT ACK") != 0:
            raise Bt05Error("Unexpected BT-05 ACK events before measurement")

        info(
            f"Starting Tor transport monitor on dedicated Chutney client {client_tor.name} "
            f"(ControlPort {client_tor.control_port})"
        )
        tor_monitor = TorConnBwMonitor(client_tor.control_port)
        tor_monitor.start()

        # CONN_BW is emitted roughly once per second. Let any pre-measurement
        # partial bucket flush, then mark the benchmark window. The remaining
        # boundary uncertainty is <= about one event interval and is negligible
        # for the multi-hour final run; raw events are retained for audit.
        time.sleep(1.25)
        tor_mark = tor_monitor.mark()
        tor_traffic_before = tor_getinfo_traffic(client_tor.control_port)

        capture, _ = start_server_edge_capture(server_ip, mode_dir / "server_edge.pcap")
        measurement_started = time.monotonic()

        queue_messages(client_a, messages, message_size)
        run_meta.messages_queued = messages

        effective_timeout = (
            timeout_seconds
            if timeout_seconds is not None
            else auto_timeout_seconds(messages, mode, cbr_interval_ms, poisson_lambda)
        )
        rx_count, ack_count = wait_for_series_completion(
            client_a,
            client_b,
            messages,
            effective_timeout,
            progress_interval_seconds,
        )
        run_meta.messages_received = rx_count
        run_meta.acks_delivered = ack_count

        queued, received, acked = validate_message_identity_sets(client_a, client_b, messages)
        run_meta.unique_message_ids = len({msg_id for _, msg_id in queued})
        write_event_snapshot(mode_dir / "message_events.json", queued, received, acked)

        run_meta.duration_seconds = time.monotonic() - measurement_started

        # Allow the final ~1 s CONN_BW bucket to be reported after the last ACK.
        time.sleep(1.25)
        assert tor_monitor is not None
        tor_events = tor_monitor.since(tor_mark)
        tor_monitor.stop()
        tor_monitor = None

        tor_event_count, tor_read, tor_written = write_tor_conn_bw(
            mode_dir / "tor_conn_bw.csv", tor_events
        )
        run_meta.tor_or_event_count = tor_event_count
        run_meta.tor_or_read_bytes = tor_read
        run_meta.tor_or_written_bytes = tor_written

        tor_traffic_after = tor_getinfo_traffic(client_tor.control_port)
        if tor_traffic_before is not None:
            run_meta.tor_traffic_read_delta = max(0, tor_traffic_after[0] - tor_traffic_before[0])
            run_meta.tor_traffic_written_delta = max(0, tor_traffic_after[1] - tor_traffic_before[1])

        # Primary BT-05 metric: encrypted Tor transport bytes on TYPE=OR
        # connections of the dedicated Chutney client node. This excludes the
        # local SOCKS leg and the post-Tor server edge.
        v_total = tor_read + tor_written
        run_meta.v_total_bytes = v_total

        stop_capture(capture)
        capture = None
        packets, server_edge_bytes = pcap_wire_volume(mode_dir / "server_edge.pcap")
        run_meta.captured_packets = packets
        run_meta.server_edge_packets = packets
        run_meta.server_edge_bytes = server_edge_bytes

        # The transport measurement has ended. Calibrate again now so BT-06
        # can account for clock offset drift without adding calibration-time
        # scheduler traffic to BT-05 V_total.
        info("Calibrating VM clocks against the host after the measured series...")
        clock_calibration["post"]["client_a"] = calibrate_vm_clock(
            client_a_console, CLIENT_A_VM
        )
        clock_calibration["post"]["client_b"] = calibrate_vm_clock(
            client_b_console, CLIENT_B_VM
        )
        (mode_dir / "clock_calibration.json").write_text(
            json.dumps(clock_calibration, indent=2) + "\n",
            encoding="utf-8",
        )

        if v_total <= 0:
            raise Bt05Error("Tor OR V_total is zero")
        run_meta.overhead_percent = ((v_total - run_meta.v_user_bytes) / v_total) * 100.0
        run_meta.expansion_factor = v_total / run_meta.v_user_bytes

        run_meta.client_a_alive_end = client_alive(client_a_console)
        run_meta.client_b_alive_end = client_alive(client_b_console)
        run_meta.server_alive_end = server_alive(server_console)
        if not (
            run_meta.client_a_alive_end
            and run_meta.client_b_alive_end
            and run_meta.server_alive_end
        ):
            raise Bt05Error("Client or server process died during BT-05")

        summary = {
            "mode": mode,
            "messages": messages,
            "payload_size": message_size,
            "V_user": run_meta.v_user_bytes,
            "V_total": v_total,
            "overhead_percent": run_meta.overhead_percent,
            "expansion_factor": run_meta.expansion_factor,
            "captured_packets": packets,
            "duration_seconds": run_meta.duration_seconds,
            "messages_received": run_meta.messages_received,
            "acks_delivered": run_meta.acks_delivered,
            "measurement_source": "Tor ControlPort CONN_BW TYPE=OR",
            "measurement_point": (
                "dedicated Chutney client Tor node; encrypted OR-connection bytes at the "
                "client-side Tor edge"
            ),
            "tor_client_node": client_tor.name,
            "tor_client_socks_port": client_tor.socks_port,
            "tor_client_control_port": client_tor.control_port,
            "tor_or_event_count": tor_event_count,
            "tor_or_read_bytes": tor_read,
            "tor_or_written_bytes": tor_written,
            "tor_traffic_read_delta": run_meta.tor_traffic_read_delta,
            "tor_traffic_written_delta": run_meta.tor_traffic_written_delta,
            "server_edge_pcap": "server_edge.pcap",
            "server_edge_packets": packets,
            "server_edge_bytes": server_edge_bytes,
            "server_edge_capture_interface": run_meta.capture_interface,
            "server_edge_capture_filter": capture_filter,
            "cbr_interval_ms": cbr_interval_ms,
            "poisson_lambda": poisson_lambda,
        }
        (mode_dir / "volumetric_summary.json").write_text(
            json.dumps(summary, indent=2) + "\n",
            encoding="utf-8",
        )

        run_meta.result = "passed"
        ok(
            f"{mode}: V_user={run_meta.v_user_bytes} B, Tor_OR_V_total={v_total} B, "
            f"N={run_meta.overhead_percent:.4f}%, E={run_meta.expansion_factor:.4f}, "
            f"server-edge={server_edge_bytes} B"
        )

    except Exception:
        if measurement_started is not None:
            run_meta.duration_seconds = time.monotonic() - measurement_started
        run_meta.result = "failed"
        raise
    finally:
        stop_capture(capture)
        if tor_monitor is not None:
            try:
                tor_monitor.stop()
            except Exception as exc:
                warn(f"Could not stop Tor CONN_BW monitor cleanly: {exc}")
        run_meta.finished_at = datetime.now().astimezone().isoformat()

        try:
            (mode_dir / "clock_calibration.json").write_text(
                json.dumps(clock_calibration, indent=2) + "\n",
                encoding="utf-8",
            )
        except Exception as exc:
            warn(f"Could not persist clock calibration: {exc}")

        try:
            collect_client_log(client_a_console, mode_dir / "client_a.log")
        except Exception as exc:
            warn(f"Could not collect client A log: {exc}")
        try:
            collect_client_log(client_b_console, mode_dir / "client_b.log")
        except Exception as exc:
            warn(f"Could not collect client B log: {exc}")
        try:
            server_log = remote_cat(server_console, REMOTE_SERVER_LOG)
            (mode_dir / "server.log").write_text(
                server_log + ("\n" if server_log and not server_log.endswith("\n") else ""),
                encoding="utf-8",
            )
        except Exception as exc:
            warn(f"Could not collect server log: {exc}")

        (mode_dir / "run.json").write_text(
            json.dumps(asdict(run_meta), indent=2) + "\n",
            encoding="utf-8",
        )

        try:
            stop_client(client_a_console)
        except Exception:
            pass
        try:
            stop_client(client_b_console)
        except Exception:
            pass
        try:
            stop_server(server_console)
        except Exception:
            pass


def close_console(child: pexpect.spawn | None) -> None:
    if child is None:
        return
    try:
        child.sendcontrol("]")
        child.close()
    except Exception:
        child.close(force=True)


def destroy_vms() -> None:
    info("Destroying temporary benchmark VMs...")
    for vm in ALL_VMS:
        state = subprocess.run(
            ["virsh", "-c", "qemu:///system", "domstate", vm],
            text=True,
            capture_output=True,
        )
        if state.returncode != 0:
            continue
        if "running" in state.stdout.lower():
            subprocess.run(
                ["virsh", "-c", "qemu:///system", "destroy", vm],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        subprocess.run(
            [
                "virsh",
                "-c",
                "qemu:///system",
                "undefine",
                vm,
                "--nvram",
                "--remove-all-storage",
            ],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="BT-05 volumetric overhead benchmark for CBR and Poisson"
    )
    parser.add_argument("--mode", choices=["all", "cbr", "poisson"], default="all")
    parser.add_argument("--messages", type=int, default=DEFAULT_MESSAGES)
    parser.add_argument("--message-size", type=int, default=DEFAULT_MESSAGE_SIZE)
    parser.add_argument("--cbr-interval-ms", type=int, default=DEFAULT_CBR_INTERVAL_MS)
    parser.add_argument("--poisson-lambda", type=float, default=DEFAULT_POISSON_LAMBDA)
    parser.add_argument(
        "--timeout-seconds",
        type=float,
        default=0.0,
        help="0 means derive a generous timeout from message count and scheduler rate",
    )
    parser.add_argument(
        "--progress-interval-seconds",
        type=float,
        default=DEFAULT_PROGRESS_INTERVAL_SECONDS,
    )
    parser.add_argument("--skip-analysis", action="store_true")
    parser.add_argument("--keep-environment", action="store_true")
    args = parser.parse_args()

    if args.messages <= 0:
        parser.error("--messages must be > 0")
    if args.messages > 999999:
        parser.error("--messages must be <= 999999")
    if args.message_size < 32:
        parser.error("--message-size must be >= 32")
    if args.cbr_interval_ms <= 0:
        parser.error("--cbr-interval-ms must be > 0")
    if args.poisson_lambda <= 0:
        parser.error("--poisson-lambda must be > 0")
    if args.timeout_seconds < 0:
        parser.error("--timeout-seconds must be >= 0")
    if args.progress_interval_seconds <= 0:
        parser.error("--progress-interval-seconds must be > 0")

    return args


def main() -> int:
    args = parse_args()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_root = BENCHMARKS_DIR / "results" / "BT-05" / stamp
    result_root.mkdir(parents=True, exist_ok=True)

    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"],
        cwd=PROJECT_ROOT,
        text=True,
        capture_output=True,
        check=True,
    ).stdout.strip()
    git_status = subprocess.run(
        ["git", "status", "--short"],
        cwd=PROJECT_ROOT,
        text=True,
        capture_output=True,
        check=True,
    ).stdout
    dirty = bool(git_status.strip())
    (result_root / "commit.txt").write_text(commit + "\n", encoding="utf-8")
    (result_root / "git_status.txt").write_text(git_status, encoding="utf-8")

    modes = ["cbr", "poisson"] if args.mode == "all" else [args.mode]

    server_console: pexpect.spawn | None = None
    client_a_console: pexpect.spawn | None = None
    client_b_console: pexpect.spawn | None = None
    control_socat: subprocess.Popen[str] | None = None
    service_socat: subprocess.Popen[str] | None = None
    socks_socat: subprocess.Popen[str] | None = None
    firewall_added = False
    chutney_started = False
    completed: list[str] = []
    suite_result = "failed"
    client_tor: ChutneyTorNode | None = None

    try:
        require_dependencies()
        acquire_sudo()
        repair_workspace_permissions()
        cleanup_stale_chutney_tor()
        recreate_vms()

        server_ip = wait_for_vm_ip(SERVER_VM)
        client_a_ip = wait_for_vm_ip(CLIENT_A_VM)
        client_b_ip = wait_for_vm_ip(CLIENT_B_VM)

        start_chutney()
        chutney_started = True
        client_tor = discover_dedicated_client_tor()
        check_socks5(client_tor.socks_port)
        firewall_added = ensure_firewall_rule()
        stop_stale_socat()

        control_socat = start_socat(
            [
                f"TCP-LISTEN:{SERVER_TOR_CONTROL_PORT},bind={GATEWAY_IP},fork,reuseaddr",
                f"TCP:127.0.0.1:{SERVER_TOR_CONTROL_PORT}",
            ],
            result_root / "socat_control.log",
        )
        service_socat = start_socat(
            [
                f"TCP-LISTEN:{SERVER_PORT},bind=127.0.0.1,fork,reuseaddr",
                f"TCP:{server_ip}:{SERVER_PORT}",
            ],
            result_root / "socat_service.log",
        )
        socks_socat = start_socat(
            [
                f"TCP-LISTEN:{VM_TOR_SOCKS_PORT},bind={GATEWAY_IP},fork,reuseaddr",
                f"TCP:127.0.0.1:{client_tor.socks_port}",
            ],
            result_root / "socat_socks.log",
        )

        server_console = login_to_console(SERVER_VM)
        client_a_console = login_to_console(CLIENT_A_VM)
        client_b_console = login_to_console(CLIENT_B_VM)
        assert_client_binary_has_diagnostics(client_a_console, CLIENT_A_VM)
        assert_client_binary_has_diagnostics(client_b_console, CLIENT_B_VM)

        for mode in modes:
            run_mode(
                mode=mode,
                messages=args.messages,
                message_size=args.message_size,
                cbr_interval_ms=args.cbr_interval_ms,
                poisson_lambda=args.poisson_lambda,
                timeout_seconds=(args.timeout_seconds or None),
                progress_interval_seconds=args.progress_interval_seconds,
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

        if not args.skip_analysis and set(modes) == {"cbr", "poisson"}:
            info("Running BT-05 aggregate analysis...")
            result = subprocess.run(
                [sys.executable, str(ANALYZE), str(result_root)],
                cwd=str(PROJECT_ROOT),
            )
            if result.returncode != 0:
                raise Bt05Error(f"BT-05 analysis exited with code {result.returncode}")
        elif not args.skip_analysis:
            info("Skipping aggregate comparison because only one mode was requested")

        suite_result = "passed"
        print("\nBT-05 functional execution: PASS", flush=True)
        print(
            "[i] Overhead values are experimental results; no target N is used as a PASS/FAIL gate.",
            flush=True,
        )
        return 0

    except KeyboardInterrupt:
        suite_result = "interrupted"
        return 130
    except Exception as exc:
        warn(str(exc))
        suite_result = "failed"
        return 1
    finally:
        suite_meta = {
            "started_at": stamp,
            "commit": commit,
            "dirty_worktree": dirty,
            "modes_requested": modes,
            "completed": completed,
            "messages": args.messages,
            "message_size": args.message_size,
            "V_user": args.messages * args.message_size,
            "cbr_interval_ms": args.cbr_interval_ms,
            "poisson_lambda": args.poisson_lambda,
            "tor_client_node": client_tor.name if client_tor else None,
            "tor_client_socks_port": client_tor.socks_port if client_tor else None,
            "tor_client_control_port": client_tor.control_port if client_tor else None,
            "result": suite_result,
        }
        (result_root / "suite_run.json").write_text(
            json.dumps(suite_meta, indent=2) + "\n",
            encoding="utf-8",
        )

        close_console(client_a_console)
        close_console(client_b_console)
        close_console(server_console)
        stop_process(socks_socat, "VM-to-Chutney SOCKS socat")
        stop_process(service_socat, "hidden-service socat")
        stop_process(control_socat, "control-port socat")
        remove_firewall_rule_if_added(firewall_added)

        if not args.keep_environment:
            if chutney_started:
                # Chutney stop is network-wide; stop the complete private Tor
                # network explicitly instead of pretending launch phases can
                # be stopped independently.
                stop_chutney()
            destroy_vms()
        else:
            info("Keeping VMs and Chutney running (--keep-environment)")

        info(f"Results directory: {result_root}")


if __name__ == "__main__":
    raise SystemExit(main())
