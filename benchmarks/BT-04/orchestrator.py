#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import re
import shlex
import signal
import shutil
import socket
import struct
import subprocess
import sys
import time
import uuid
from dataclasses import asdict, dataclass
from datetime import datetime
from pathlib import Path

import pexpect


PROJECT_ROOT = Path(__file__).resolve().parents[2]
BENCHMARKS_DIR = PROJECT_ROOT / "benchmarks"
TEST_DIR = BENCHMARKS_DIR / "BT-04"
VM_MANAGER = BENCHMARKS_DIR / "vm_manager.py"
CHUTNEY_MANAGER = BENCHMARKS_DIR / "chutney_manager.sh"
ANALYZE = TEST_DIR / "analyze.py"

SERVER_VM = "bc-server"
CLIENT_A_VM = "bc-client-1"
CLIENT_B_VM = "bc-client-2"
ALL_VMS = (SERVER_VM, CLIENT_A_VM, CLIENT_B_VM)

GATEWAY_IP = "192.168.122.1"
TOR_CONTROL_PORT = 8006
TOR_SOCKS_HOST = "127.0.0.1"
TOR_SOCKS_PORT = 9050
SERVER_PORT = 8080

REMOTE_SERVER_CONFIG = "/etc/blank-chat/server_config.toml"
REMOTE_SERVER_LOG = "/tmp/bt04_server.log"
REMOTE_SERVER_PID = "/tmp/bt04_server.pid"

REMOTE_CLIENT_CONFIG = "/etc/blank-chat/client_config.toml"
REMOTE_CONTACTS = "/etc/blank-chat/contacts.json"
REMOTE_IDENTITY = "/etc/blank-chat/identity.json"
REMOTE_CLIENT_FIFO = "/tmp/bt04_client.in"
REMOTE_CLIENT_LOG = "/tmp/bt04_client.log"
REMOTE_CLIENT_PID = "/tmp/bt04_client.pid"
REMOTE_CLIENT_WORKDIR = "/tmp/bt04-client"

SHELL_PROMPT = "__BC_BT04_PROMPT__ "
FRAME_HEADER_SIZE = 21
ACTION_AUTH_CHALLENGE = 0x04
CONTROL_PAYLOAD_SIZE = 498 - FRAME_HEADER_SIZE

CBR_INTERVAL_MS = 10_000
POISSON_LAMBDA = 0.1
PFS_MESSAGE_INTERVAL = 0
DEFAULT_DURATION_SECONDS = 3600
DEFAULT_CHAT_INTERVAL_SECONDS = 15.0
DEFAULT_MESSAGE_SIZE = 150
DEFAULT_INTENSIVE_BACKLOG = 1000

ACTION_NAMES = {1: "PUSH", 2: "POLL", 3: "ACK", 4: "AUTH_CHALLENGE", 5: "AUTH_RESPONSE"}
TX_RE = re.compile(r"BT04_EVENT TX timestamp_ns=(\d+) action=(\d+) bytes=(\d+)")
MNEMONIC_RE = re.compile(r"\b(?:[a-z]+-){23}[a-z]+\b")
ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")


class Bt04Error(RuntimeError):
    pass


@dataclass
class ScenarioRun:
    started_at: str
    finished_at: str = ""
    commit: str = ""
    dirty_worktree: bool = False
    mode: str = ""
    profile: str = ""
    duration_seconds: int = DEFAULT_DURATION_SECONDS
    measurement_elapsed_seconds: float = 0.0
    cbr_interval_ms: int = CBR_INTERVAL_MS
    poisson_lambda: float = POISSON_LAMBDA
    poisson_min_clamp_ms: int = 100
    pfs_message_interval: int = PFS_MESSAGE_INTERVAL
    message_interval_seconds: float | None = None
    message_size: int = DEFAULT_MESSAGE_SIZE
    messages_generated: int = 0
    intensive_backlog: int = 0
    transmissions_recorded: int = 0
    push_count: int = 0
    poll_count: int = 0
    ack_count: int = 0
    server_ip: str = ""
    client_a_ip: str = ""
    client_b_ip: str = ""
    onion: str = ""
    client_a_alive_end: bool = False
    client_b_alive_end: bool = False
    server_alive_end: bool = False
    result: str = "incomplete"


@dataclass
class ClientHandle:
    name: str
    vm: str
    peer_alias: str
    console: pexpect.spawn


def info(message: str) -> None:
    print(f"[i] {message}", flush=True)


def ok(message: str) -> None:
    print(f"[+] {message}", flush=True)


def warn(message: str) -> None:
    print(f"[!] {message}", file=sys.stderr, flush=True)


def run(cmd: list[str], *, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess[str]:
    info("$ " + shlex.join(cmd))
    result = subprocess.run(cmd, cwd=str(PROJECT_ROOT), text=True, capture_output=capture)
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip() if capture else ""
        raise Bt04Error(
            f"Command failed with exit code {result.returncode}: {shlex.join(cmd)}"
            + (f"\n{detail}" if detail else "")
        )
    return result


def require_dependencies() -> None:
    required = ("virsh", "virt-install", "qemu-img", "socat", "sudo", "iptables", "pkill")
    missing = [name for name in required if shutil.which(name) is None]
    if missing:
        raise Bt04Error("Missing host dependencies: " + ", ".join(missing))
    if shutil.which("tcpdump") is None and shutil.which("tshark") is None:
        raise Bt04Error("BT-04 requires tcpdump or tshark on the host for supplementary PCAP capture")
    for path in (VM_MANAGER, CHUTNEY_MANAGER, ANALYZE):
        if not path.exists():
            raise Bt04Error(f"Required file does not exist: {path}")


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
                st = candidate.stat()
            except FileNotFoundError:
                continue
            if st.st_uid != uid or st.st_gid != gid:
                needs_fix = True
                break
        if needs_fix:
            run(["sudo", "chown", "-R", owner, str(path)])


def cleanup_stale_chutney_tor() -> None:
    runtime = str((PROJECT_ROOT / ".chutney" / "net").resolve())
    result = subprocess.run(["ps", "-eo", "pid=,comm=,args="], text=True, capture_output=True, check=True)
    pids: list[int] = []
    for line in result.stdout.splitlines():
        parts = line.strip().split(None, 2)
        if len(parts) == 3 and parts[1] == "tor" and runtime in parts[2]:
            try:
                pids.append(int(parts[0]))
            except ValueError:
                pass
    if not pids:
        return
    run(["sudo", "kill", "-TERM", *map(str, pids)], check=False)
    time.sleep(1)


def recreate_vms() -> None:
    info("Recreating benchmark VMs from Yocto images...")
    run([sys.executable, str(VM_MANAGER)])


def get_vm_ip_once(vm_name: str) -> str | None:
    result = run(["virsh", "-c", "qemu:///system", "domifaddr", vm_name], check=False, capture=True)
    match = re.search(r"(\d+\.\d+\.\d+\.\d+)/\d+", result.stdout)
    if match:
        return match.group(1)

    result = run(["virsh", "-c", "qemu:///system", "domiflist", vm_name], check=False, capture=True)
    mac_match = re.search(r"\b([0-9a-fA-F]{2}(?::[0-9a-fA-F]{2}){5})\b", result.stdout)
    if not mac_match:
        return None
    mac = mac_match.group(1).lower()

    leases = run(["virsh", "-c", "qemu:///system", "net-dhcp-leases", "default"], check=False, capture=True)
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
    raise Bt04Error(f"Timed out waiting for IP address of {vm_name}")


def start_chutney() -> None:
    run(["bash", str(CHUTNEY_MANAGER), "clean"], check=False)
    run(["bash", str(CHUTNEY_MANAGER), "start"])
    run(["bash", str(CHUTNEY_MANAGER), "status"])


def check_socks5() -> None:
    with socket.create_connection((TOR_SOCKS_HOST, TOR_SOCKS_PORT), timeout=5) as sock:
        sock.sendall(b"\x05\x01\x00")
        if sock.recv(2) != b"\x05\x00":
            raise Bt04Error("Chutney SOCKS5 is not ready")
    ok("Chutney SOCKS5 is ready")


def ensure_firewall_rule() -> bool:
    result = subprocess.run(
        ["sudo", "iptables", "-C", "INPUT", "-i", "virbr0", "-j", "ACCEPT"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if result.returncode == 0:
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
    for pattern in (
        r"socat TCP-LISTEN:8006,bind=192\.168\.122\.1",
        r"socat TCP-LISTEN:8080,bind=127\.0\.0\.1",
        r"socat TCP-LISTEN:9050,bind=192\.168\.122\.1",
    ):
        subprocess.run(["pkill", "-f", pattern], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)


def start_socat(args: list[str], log_path: Path) -> subprocess.Popen[str]:
    log_file = log_path.open("w", encoding="utf-8")
    process = subprocess.Popen(["socat", *args], stdout=log_file, stderr=subprocess.STDOUT, text=True)
    log_file.close()
    time.sleep(0.5)
    if process.poll() is not None:
        raise Bt04Error(f"socat failed: {log_path.read_text(encoding='utf-8', errors='replace')}")
    ok("Started socat: " + " ".join(args))
    return process


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
        "virsh", ["-c", "qemu:///system", "console", vm_name], encoding="utf-8", timeout=10
    )
    try:
        child.expect(r"Escape character is", timeout=20)
    except pexpect.TIMEOUT as exc:
        child.close(force=True)
        raise Bt04Error(f"Could not attach to {vm_name} serial console") from exc

    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        child.send("\r\n")
        index = child.expect([r"(?i)login:", r"root@[^#\r\n]*#", r"# ", pexpect.TIMEOUT, pexpect.EOF], timeout=3)
        if index == 0:
            child.send("root\r\n")
            continue
        if index in (1, 2):
            break
        if index == 4:
            child.close(force=True)
            raise Bt04Error(f"Console for {vm_name} closed during login")
    else:
        child.close(force=True)
        raise Bt04Error(f"Timed out logging in to {vm_name}")

    child.send("stty -echo\r\n")
    child.expect(r"# ", timeout=10)
    child.send(f"export PS1='{SHELL_PROMPT}'\r\n")
    child.expect(re.escape(SHELL_PROMPT), timeout=10)
    ok(f"Logged into {vm_name} as root")
    return child


def console_cmd(child: pexpect.spawn, command: str, *, timeout: float = 60.0, check: bool = True) -> tuple[str, int]:
    token = uuid.uuid4().hex
    begin = f"__BC_BEGIN_{token}__"
    end = f"__BC_END_{token}__"
    wrapped = (
        f"printf '\\n{begin}\\n'; {command}; __bc_rc=$?; "
        f"printf '\\n{end}:%s\\n' \"$__bc_rc\""
    )
    child.send(wrapped + "\r\n")
    child.expect(re.escape(begin), timeout=timeout)
    child.expect(re.escape(end) + r":(\d+)", timeout=timeout)
    output = child.before.replace("\r", "").strip()
    rc = int(child.match.group(1))
    child.expect(re.escape(SHELL_PROMPT), timeout=10)
    if check and rc != 0:
        raise Bt04Error(f"Command failed inside VM (rc={rc}): {command}\n{output}")
    return output, rc


def upload_text(child: pexpect.spawn, remote_path: str, text: str) -> None:
    quoted = shlex.quote(remote_path)
    console_cmd(child, f": > {quoted}")
    for line in text.splitlines():
        console_cmd(child, f"printf '%s\\n' {shlex.quote(line)} >> {quoted}")


def remote_cat(child: pexpect.spawn, path: str) -> str:
    output, _ = console_cmd(child, f"cat {shlex.quote(path)} 2>/dev/null || true", check=False)
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
    console_cmd(child, f"kill $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null || true", check=False)
    time.sleep(0.5)


def wait_for_onion(child: pexpect.spawn, timeout: float = 60.0) -> str:
    deadline = time.monotonic() + timeout
    onion_re = re.compile(r"\b([a-z2-7]{56})(?:\.onion)?\b")
    while time.monotonic() < deadline:
        output, _ = console_cmd(
            child,
            f"grep -Eo '[a-z2-7]{{56}}(\\.onion)?' {REMOTE_SERVER_LOG} 2>/dev/null | tail -n 1 || true",
            check=False,
        )
        match = onion_re.search(output)
        if match:
            onion = match.group(1) + ".onion"
            ok(f"Hidden service: {onion}")
            return onion
        time.sleep(1)
    raise Bt04Error("Timed out waiting for hidden-service address")


def recv_exact(sock: socket.socket, size: int) -> bytes:
    data = bytearray()
    while len(data) < size:
        chunk = sock.recv(size - len(data))
        if not chunk:
            raise ConnectionError("connection closed")
        data.extend(chunk)
    return bytes(data)


def socks5_connect(onion: str, target_port: int = 80, timeout: float = 5.0) -> socket.socket:
    domain = onion.encode("ascii")
    sock = socket.create_connection((TOR_SOCKS_HOST, TOR_SOCKS_PORT), timeout=timeout)
    sock.settimeout(timeout)
    try:
        sock.sendall(b"\x05\x01\x00")
        if recv_exact(sock, 2) != b"\x05\x00":
            raise Bt04Error("SOCKS5 NoAuth rejected")
        sock.sendall(b"\x05\x01\x00\x03" + bytes([len(domain)]) + domain + struct.pack(">H", target_port))
        version, reply, reserved, atyp = recv_exact(sock, 4)
        if version != 5 or reserved != 0 or reply != 0:
            raise Bt04Error(f"SOCKS5 CONNECT failed: 0x{reply:02x}")
        if atyp == 1:
            recv_exact(sock, 4)
        elif atyp == 3:
            recv_exact(sock, recv_exact(sock, 1)[0])
        elif atyp == 4:
            recv_exact(sock, 16)
        else:
            raise Bt04Error("Unsupported SOCKS5 ATYP")
        recv_exact(sock, 2)
        return sock
    except Exception:
        sock.close()
        raise


def wait_for_hidden_service(onion: str, timeout: float = 60.0) -> None:
    info("Waiting for hidden service through Chutney...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            sock = socks5_connect(onion)
            try:
                header = recv_exact(sock, FRAME_HEADER_SIZE)
                action = header[0]
                length = struct.unpack("<I", header[17:21])[0]
                payload = recv_exact(sock, length) if length else b""
                if action == ACTION_AUTH_CHALLENGE and len(payload) == CONTROL_PAYLOAD_SIZE:
                    ok("Hidden service reachable")
                    return
            finally:
                sock.close()
        except Exception:
            pass
        time.sleep(2)
    raise Bt04Error("Hidden service did not become reachable")


def configure_client(child: pexpect.spawn, onion: str, mode: str) -> None:
    config = f'''[network]
tor_socks_host = "{GATEWAY_IP}"
tor_socks_port = {TOR_SOCKS_PORT}

[relay]
onion_address = "{onion}"
onion_port = 80

[obfuscation]
mode = "{mode}"
cbr_interval_ms = {CBR_INTERVAL_MS}
poisson_lambda = {POISSON_LAMBDA}

[storage]
contacts_file_path = "{REMOTE_CONTACTS}"

[security]
pfs_message_interval = {PFS_MESSAGE_INTERVAL}
'''
    console_cmd(child, "mkdir -p /etc/blank-chat")
    upload_text(child, REMOTE_CLIENT_CONFIG, config)


def assert_client_binary_has_diagnostics(child: pexpect.spawn, vm_name: str) -> None:
    _, rc = console_cmd(
        child,
        "grep -q 'BT04_EVENT TX' /usr/bin/blank_chat_client 2>/dev/null",
        check=False,
    )
    if rc != 0:
        raise Bt04Error(
            f"{vm_name} does not contain BT-04 diagnostics. Run "
            "'./benchmarks/BT-04/run.sh --prepare --duration-seconds 90' first."
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


def wait_client_log_count(child: pexpect.spawn, pattern: str, minimum: int, timeout: float = 60.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if client_log_count(child, pattern) >= minimum:
            return
        time.sleep(0.25)
    raise Bt04Error(f"Timed out waiting for client log: {pattern}")


def wait_client_log(child: pexpect.spawn, pattern: str, timeout: float = 60.0) -> None:
    wait_client_log_count(child, pattern, 1, timeout)


def start_client(child: pexpect.spawn, vm_name: str) -> None:
    info(f"Starting blank_chat_client on {vm_name}...")
    console_cmd(child, f"kill $(cat {REMOTE_CLIENT_PID} 2>/dev/null) 2>/dev/null || true", check=False)
    console_cmd(
        child,
        f"rm -f {REMOTE_IDENTITY} {REMOTE_CONTACTS} {REMOTE_CLIENT_FIFO} {REMOTE_CLIENT_LOG} {REMOTE_CLIENT_PID}",
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
    console_cmd(child, f"kill $(cat {REMOTE_CLIENT_PID} 2>/dev/null) 2>/dev/null || true", check=False)
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
        raise Bt04Error("Could not parse BIP39 mnemonic")
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
    expected = "Initializing Constant Bit Rate" if mode == "cbr" else "Initializing Stochastic Poisson Obfuscator"
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
            raise Bt04Error("A client exited while waiting for initial PFS")
        time.sleep(1)
    raise Bt04Error("Initial PFS did not complete within timeout")


def get_tx_events(child: pexpect.spawn) -> list[tuple[int, int, int]]:
    output, _ = console_cmd(
        child,
        f"grep 'BT04_EVENT TX' {REMOTE_CLIENT_LOG} 2>/dev/null || true",
        timeout=90,
        check=False,
    )
    events: list[tuple[int, int, int]] = []
    for line in strip_ansi(output).splitlines():
        match = TX_RE.search(line)
        if match:
            events.append(tuple(map(int, match.groups())))
    return events


def make_message(prefix: str, sequence: int, size: int) -> str:
    base = f"{prefix}_{sequence:06d}_"
    if len(base) > size:
        raise Bt04Error("Configured message size is too small for benchmark prefix")
    return base + ("X" * (size - len(base)))


def queue_intensive_backlog(client: ClientHandle, count: int, size: int) -> None:
    info(f"Queuing intensive backlog: {count} messages of {size} B")
    payload = "I" * size
    before = client_log_count(client.console, "Message queued for transmission.")
    quoted_command = shlex.quote(f"send {client.peer_alias} {payload}")
    console_cmd(
        client.console,
        f"i=0; while [ \"$i\" -lt {count} ]; do printf '%s\\n' {quoted_command} >&3; i=$((i+1)); done",
        timeout=300,
    )
    wait_client_log_count(
        client.console,
        "Message queued for transmission.",
        before + count,
        timeout=300,
    )
    ok("Intensive backlog queued")


def start_capture(client_a_ip: str, path: Path) -> subprocess.Popen:
    if shutil.which("tcpdump"):
        cmd = ["sudo", "tcpdump", "-i", "virbr0", "-nn", "-s", "0", "host", client_a_ip, "-w", str(path)]
    else:
        cmd = ["sudo", "tshark", "-i", "virbr0", "-f", f"host {client_a_ip}", "-w", str(path)]
    info("Starting supplementary PCAP capture: " + shlex.join(cmd))
    proc = subprocess.Popen(cmd, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1)
    if proc.poll() is not None:
        raise Bt04Error("PCAP capture process exited immediately")
    return proc


def stop_capture(proc: subprocess.Popen | None) -> None:
    if proc is None or proc.poll() is not None:
        return
    proc.send_signal(signal.SIGINT)
    try:
        proc.wait(timeout=5)
    except subprocess.TimeoutExpired:
        proc.terminate()
        try:
            proc.wait(timeout=3)
        except subprocess.TimeoutExpired:
            proc.kill()
            proc.wait(timeout=2)


def run_profile(
    profile: str,
    sender: ClientHandle,
    duration_seconds: int,
    chat_interval_seconds: float,
    message_size: int,
) -> int:
    started = time.monotonic()
    deadline = started + duration_seconds
    generated = 0
    next_chat = started + chat_interval_seconds
    next_alive_check = started + 10.0

    while True:
        now = time.monotonic()
        if now >= deadline:
            break

        if profile == "chat" and now >= next_chat:
            generated += 1
            payload = make_message("BC_CHAT_TEST", generated, message_size)
            client_command(sender.console, f"send {sender.peer_alias} {payload}")
            next_chat += chat_interval_seconds
            continue

        if now >= next_alive_check:
            if not client_alive(sender.console):
                raise Bt04Error("Sender client exited during measurement")
            next_alive_check += 10.0

        sleep_until = deadline
        if profile == "chat":
            sleep_until = min(sleep_until, next_chat)
        sleep_until = min(sleep_until, next_alive_check)
        time.sleep(max(0.05, min(1.0, sleep_until - now)))

    return generated


def write_transmissions(path: Path, events: list[tuple[int, int, int]]) -> dict[str, int]:
    filtered = [event for event in events if event[1] in (1, 2, 3)]
    counts = {"PUSH": 0, "POLL": 0, "ACK": 0}
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.writer(handle)
        writer.writerow(["sequence", "timestamp_ns", "relative_s", "frame_type", "action", "bytes"])
        first = filtered[0][0] if filtered else 0
        for sequence, (timestamp_ns, action, size) in enumerate(filtered, start=1):
            frame_type = ACTION_NAMES.get(action, f"ACTION_{action}")
            if frame_type in counts:
                counts[frame_type] += 1
            writer.writerow(
                [sequence, timestamp_ns, f"{(timestamp_ns - first) / 1_000_000_000.0:.9f}", frame_type, action, size]
            )
    counts["TOTAL"] = len(filtered)
    return counts


def validate_profile(profile: str, counts: dict[str, int]) -> None:
    if counts["TOTAL"] < 2:
        raise Bt04Error("Too few application TX events were recorded")
    if profile == "idle" and counts["PUSH"] != 0:
        raise Bt04Error("Idle profile emitted PUSH frames during the measurement window")
    if profile == "chat" and counts["PUSH"] == 0:
        raise Bt04Error("Chat profile produced no PUSH transmissions")
    if profile == "intensive" and counts["PUSH"] != counts["TOTAL"]:
        raise Bt04Error(
            "Intensive profile did not keep the measured sender queue continuously non-empty "
            f"(PUSH={counts['PUSH']}, total={counts['TOTAL']})"
        )


def collect_client_log(child: pexpect.spawn, destination: Path) -> None:
    text = remote_cat(child, REMOTE_CLIENT_LOG)
    destination.write_text(strip_ansi(text) + ("\n" if text and not text.endswith("\n") else ""), encoding="utf-8")


def run_scenario(
    *,
    mode: str,
    profile: str,
    duration_seconds: int,
    chat_interval_seconds: float,
    message_size: int,
    intensive_backlog: int,
    commit: str,
    dirty: bool,
    server_ip: str,
    client_a_ip: str,
    client_b_ip: str,
    server_console: pexpect.spawn,
    client_a_console: pexpect.spawn,
    client_b_console: pexpect.spawn,
    scenario_dir: Path,
) -> None:
    scenario_dir.mkdir(parents=True, exist_ok=True)
    info(f"=== BT-04 scenario: {mode}/{profile} ({duration_seconds}s) ===")

    run_meta = ScenarioRun(
        started_at=datetime.now().astimezone().isoformat(),
        commit=commit,
        dirty_worktree=dirty,
        mode=mode,
        profile=profile,
        duration_seconds=duration_seconds,
        message_interval_seconds=chat_interval_seconds if profile == "chat" else None,
        message_size=message_size,
        intensive_backlog=intensive_backlog if profile == "intensive" else 0,
        server_ip=server_ip,
        client_a_ip=client_a_ip,
        client_b_ip=client_b_ip,
    )

    capture: subprocess.Popen | None = None
    client_a = ClientHandle("A", CLIENT_A_VM, "bob", client_a_console)
    client_b = ClientHandle("B", CLIENT_B_VM, "alice", client_b_console)

    try:
        configure_server(server_console)
        start_server(server_console)
        onion = wait_for_onion(server_console)
        wait_for_hidden_service(onion)
        run_meta.onion = onion

        configure_client(client_a_console, onion, mode)
        configure_client(client_b_console, onion, mode)
        start_client(client_a_console, CLIENT_A_VM)
        start_client(client_b_console, CLIENT_B_VM)

        mnemonic_a = extract_mnemonic(client_a_console)
        mnemonic_b = extract_mnemonic(client_b_console)
        add_contact(client_a_console, "bob", mnemonic_b)
        add_contact(client_b_console, "alice", mnemonic_a)
        connect_client(client_a_console, mode)
        connect_client(client_b_console, mode)
        wait_for_initial_pfs(client_a, client_b)

        if profile == "intensive":
            queue_intensive_backlog(client_a, intensive_backlog, message_size)

        capture = start_capture(client_a_ip, scenario_dir / "traffic.pcap")
        baseline_events = len(get_tx_events(client_a_console))

        measurement_started = time.monotonic()
        run_meta.messages_generated = run_profile(
            profile,
            client_a,
            duration_seconds,
            chat_interval_seconds,
            message_size,
        )
        run_meta.measurement_elapsed_seconds = time.monotonic() - measurement_started

        stop_capture(capture)
        capture = None

        all_events = get_tx_events(client_a_console)
        measured_events = all_events[baseline_events:]
        counts = write_transmissions(scenario_dir / "transmissions.csv", measured_events)
        validate_profile(profile, counts)

        run_meta.transmissions_recorded = counts["TOTAL"]
        run_meta.push_count = counts["PUSH"]
        run_meta.poll_count = counts["POLL"]
        run_meta.ack_count = counts["ACK"]
        run_meta.client_a_alive_end = client_alive(client_a_console)
        run_meta.client_b_alive_end = client_alive(client_b_console)
        run_meta.server_alive_end = server_alive(server_console)

        if not (run_meta.client_a_alive_end and run_meta.client_b_alive_end and run_meta.server_alive_end):
            raise Bt04Error("Client or server process died during scenario")

        run_meta.result = "passed"
        ok(
            f"{mode}/{profile}: tx={counts['TOTAL']} PUSH={counts['PUSH']} "
            f"POLL={counts['POLL']} ACK={counts['ACK']}"
        )

    except Exception:
        run_meta.result = "failed"
        raise
    finally:
        stop_capture(capture)
        run_meta.finished_at = datetime.now().astimezone().isoformat()

        try:
            collect_client_log(client_a_console, scenario_dir / "client_a.log")
        except Exception as exc:
            warn(f"Could not collect client A log: {exc}")
        try:
            collect_client_log(client_b_console, scenario_dir / "client_b.log")
        except Exception as exc:
            warn(f"Could not collect client B log: {exc}")
        try:
            server_log = remote_cat(server_console, REMOTE_SERVER_LOG)
            (scenario_dir / "server.log").write_text(server_log + ("\n" if server_log else ""), encoding="utf-8")
        except Exception as exc:
            warn(f"Could not collect server log: {exc}")

        (scenario_dir / "run.json").write_text(json.dumps(asdict(run_meta), indent=2) + "\n", encoding="utf-8")

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
        state = subprocess.run(["virsh", "-c", "qemu:///system", "domstate", vm], text=True, capture_output=True)
        if state.returncode != 0:
            continue
        if "running" in state.stdout.lower():
            subprocess.run(["virsh", "-c", "qemu:///system", "destroy", vm], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        subprocess.run(
            ["virsh", "-c", "qemu:///system", "undefine", vm, "--nvram", "--remove-all-storage"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BT-04 VM-based CBR/Poisson traffic timing benchmark")
    parser.add_argument("--mode", choices=["all", "cbr", "poisson"], default="all")
    parser.add_argument("--profile", choices=["all", "idle", "chat", "intensive"], default="all")
    parser.add_argument("--duration-seconds", type=int, default=DEFAULT_DURATION_SECONDS)
    parser.add_argument("--chat-interval-seconds", type=float, default=DEFAULT_CHAT_INTERVAL_SECONDS)
    parser.add_argument("--message-size", type=int, default=DEFAULT_MESSAGE_SIZE)
    parser.add_argument("--intensive-backlog", type=int, default=DEFAULT_INTENSIVE_BACKLOG)
    parser.add_argument("--skip-analysis", action="store_true")
    parser.add_argument("--keep-environment", action="store_true")
    args = parser.parse_args()
    if args.duration_seconds < 30:
        parser.error("--duration-seconds must be >= 30")
    if args.chat_interval_seconds <= 0:
        parser.error("--chat-interval-seconds must be > 0")
    if args.message_size < 32:
        parser.error("--message-size must be >= 32")
    if args.intensive_backlog < 100:
        parser.error("--intensive-backlog must be >= 100")
    return args


def main() -> int:
    args = parse_args()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_root = BENCHMARKS_DIR / "results" / "BT-04" / stamp
    result_root.mkdir(parents=True, exist_ok=True)

    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROJECT_ROOT, text=True, capture_output=True, check=True).stdout.strip()
    git_status = subprocess.run(["git", "status", "--short"], cwd=PROJECT_ROOT, text=True, capture_output=True, check=True).stdout
    dirty = bool(git_status.strip())
    (result_root / "commit.txt").write_text(commit + "\n", encoding="utf-8")
    (result_root / "git_status.txt").write_text(git_status, encoding="utf-8")

    modes = ["cbr", "poisson"] if args.mode == "all" else [args.mode]
    profiles = ["idle", "chat", "intensive"] if args.profile == "all" else [args.profile]

    server_console: pexpect.spawn | None = None
    client_a_console: pexpect.spawn | None = None
    client_b_console: pexpect.spawn | None = None
    control_socat: subprocess.Popen | None = None
    service_socat: subprocess.Popen | None = None
    socks_socat: subprocess.Popen | None = None
    firewall_added = False
    chutney_started = False
    completed: list[str] = []
    result = "failed"

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
        check_socks5()
        firewall_added = ensure_firewall_rule()
        stop_stale_socat()

        control_socat = start_socat(
            [f"TCP-LISTEN:{TOR_CONTROL_PORT},bind={GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{TOR_CONTROL_PORT}"],
            result_root / "socat_control.log",
        )
        service_socat = start_socat(
            [f"TCP-LISTEN:{SERVER_PORT},bind=127.0.0.1,fork,reuseaddr", f"TCP:{server_ip}:{SERVER_PORT}"],
            result_root / "socat_service.log",
        )
        socks_socat = start_socat(
            [f"TCP-LISTEN:{TOR_SOCKS_PORT},bind={GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{TOR_SOCKS_PORT}"],
            result_root / "socat_socks.log",
        )

        server_console = login_to_console(SERVER_VM)
        client_a_console = login_to_console(CLIENT_A_VM)
        client_b_console = login_to_console(CLIENT_B_VM)
        assert_client_binary_has_diagnostics(client_a_console, CLIENT_A_VM)
        assert_client_binary_has_diagnostics(client_b_console, CLIENT_B_VM)

        for mode in modes:
            for profile in profiles:
                scenario_dir = result_root / mode / profile
                run_scenario(
                    mode=mode,
                    profile=profile,
                    duration_seconds=args.duration_seconds,
                    chat_interval_seconds=args.chat_interval_seconds,
                    message_size=args.message_size,
                    intensive_backlog=args.intensive_backlog,
                    commit=commit,
                    dirty=dirty,
                    server_ip=server_ip,
                    client_a_ip=client_a_ip,
                    client_b_ip=client_b_ip,
                    server_console=server_console,
                    client_a_console=client_a_console,
                    client_b_console=client_b_console,
                    scenario_dir=scenario_dir,
                )
                completed.append(f"{mode}/{profile}")

        if not args.skip_analysis:
            info("Running BT-04 aggregate analysis...")
            analysis = subprocess.run([sys.executable, str(ANALYZE), str(result_root)], cwd=str(PROJECT_ROOT))
            if analysis.returncode != 0:
                raise Bt04Error(f"BT-04 analysis exited with code {analysis.returncode}")

        result = "passed"
        print("\nBT-04 functional execution: PASS", flush=True)
        print("[i] Idle-vs-Chat KS results are reported, not used as a PASS/FAIL gate.", flush=True)
        return 0

    except KeyboardInterrupt:
        result = "interrupted"
        return 130
    except Exception as exc:
        warn(str(exc))
        result = "failed"
        return 1
    finally:
        matrix_meta = {
            "started_at": stamp,
            "commit": commit,
            "dirty_worktree": dirty,
            "duration_seconds_per_scenario": args.duration_seconds,
            "modes_requested": modes,
            "profiles_requested": profiles,
            "completed": completed,
            "result": result,
        }
        (result_root / "matrix_run.json").write_text(json.dumps(matrix_meta, indent=2) + "\n", encoding="utf-8")

        close_console(client_a_console)
        close_console(client_b_console)
        close_console(server_console)
        stop_process(socks_socat, "VM-to-Chutney SOCKS socat")
        stop_process(service_socat, "hidden-service socat")
        stop_process(control_socat, "control-port socat")
        remove_firewall_rule_if_added(firewall_added)

        if not args.keep_environment:
            if chutney_started:
                subprocess.run(["bash", str(CHUTNEY_MANAGER), "stop"], cwd=str(PROJECT_ROOT), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            destroy_vms()
        else:
            info("Keeping VMs and Chutney running (--keep-environment)")

        info(f"Results directory: {result_root}")


if __name__ == "__main__":
    raise SystemExit(main())
