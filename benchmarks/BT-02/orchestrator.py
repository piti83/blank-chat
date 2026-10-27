#!/usr/bin/env python3
from __future__ import annotations

import argparse
import ctypes
import ctypes.util
import hashlib
import json
import os
import re
import shlex
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
VM_MANAGER = BENCHMARKS_DIR / "vm_manager.py"
CHUTNEY_MANAGER = BENCHMARKS_DIR / "chutney_manager.sh"

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
REMOTE_SERVER_LOG = "/tmp/bt02_server.log"
REMOTE_SERVER_PID = "/tmp/bt02_server.pid"

REMOTE_CLIENT_CONFIG = "/etc/blank-chat/client_config.toml"
REMOTE_CONTACTS = "/etc/blank-chat/contacts.json"
REMOTE_IDENTITY = "/etc/blank-chat/identity.json"
REMOTE_CLIENT_FIFO = "/tmp/bt02_client.in"
REMOTE_CLIENT_LOG = "/tmp/bt02_client.log"
REMOTE_CLIENT_PID = "/tmp/bt02_client.pid"
REMOTE_CLIENT_WORKDIR = "/tmp/bt02-client"

SHELL_PROMPT = "__BC_BT02_PROMPT__ "
FRAME_HEADER_SIZE = 21
ACTION_AUTH_CHALLENGE = 0x04
CONTROL_PAYLOAD_SIZE = 498 - FRAME_HEADER_SIZE

KEY_SIZE = 32
NONCE_SIZE = 24
MAC_SIZE = 16
PADDING_MARKER = 0x80
TEXT_MESSAGE_OPCODE = 0x01

ANSI_RE = re.compile(r"\x1b\[[0-9;?]*[ -/]*[@-~]")
MNEMONIC_RE = re.compile(r"\b(?:[a-z]+-){23}[a-z]+\b")
EVENT_RE = re.compile(r"BT02_EVENT\s+([A-Z0-9_]+)\s+(.*)$")
KV_RE = re.compile(r"([a-z_]+)=([^\s]+)")

RUN_LOG: Path | None = None
RAW_KEY_BUFFERS: list[bytearray] = []


class Bt02Error(RuntimeError):
    pass


@dataclass
class PublicContactState:
    tx_fingerprint: str
    rx_fingerprint: str
    tx_mailbox: str
    rx_mailbox: str
    initial_pfs_complete: bool


@dataclass
class RawContactState:
    tx_key: bytearray
    rx_key: bytearray
    tx_mailbox: str
    rx_mailbox: str
    initial_pfs_complete: bool

    def public(self) -> PublicContactState:
        return PublicContactState(
            tx_fingerprint=blake2b16(bytes(self.tx_key)),
            rx_fingerprint=blake2b16(bytes(self.rx_key)),
            tx_mailbox=self.tx_mailbox,
            rx_mailbox=self.rx_mailbox,
            initial_pfs_complete=self.initial_pfs_complete,
        )


@dataclass
class EpochState:
    epoch: int
    a: PublicContactState
    b: PublicContactState


@dataclass
class RotationEvidence:
    rotation: int
    initiator: str
    responder: str
    old_epoch: int
    new_epoch: int
    initiator_ephemeral_fingerprint: str
    responder_ephemeral_fingerprint: str
    request_signature_verified: bool
    ack_signature_verified: bool
    old_rx_retained: bool
    old_rx_message_accepted: bool
    old_rx_retired: bool
    old_ciphertext_with_old_key: bool
    old_ciphertext_with_new_key: bool
    new_ciphertext_with_new_key: bool
    new_ciphertext_with_old_key: bool
    old_ciphertext_fingerprint: str
    new_ciphertext_fingerprint: str


@dataclass
class RunMetadata:
    started_at: str
    commit: str = ""
    dirty_worktree: bool = False
    server_ip: str = ""
    client_a_ip: str = ""
    client_b_ip: str = ""
    onion: str = ""
    pfs_message_interval: int = 2
    cbr_interval_ms: int = 250
    rotations_requested: int = 3
    rotations_completed: int = 0
    result: str = "incomplete"


@dataclass
class ClientHandle:
    name: str
    vm: str
    alias_for_peer: str
    local_alias_seen_by_peer: str
    console: pexpect.spawn


def _write_log(line: str) -> None:
    if RUN_LOG is not None:
        with RUN_LOG.open("a", encoding="utf-8") as handle:
            handle.write(line + "\n")


def info(message: str) -> None:
    line = f"[i] {message}"
    print(line, flush=True)
    _write_log(line)


def ok(message: str) -> None:
    line = f"[+] {message}"
    print(line, flush=True)
    _write_log(line)


def warn(message: str) -> None:
    line = f"[!] {message}"
    print(line, file=sys.stderr, flush=True)
    _write_log(line)


def run(cmd: list[str], *, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess[str]:
    info("$ " + shlex.join(cmd))
    result = subprocess.run(cmd, cwd=str(PROJECT_ROOT), text=True, capture_output=capture)
    if check and result.returncode != 0:
        detail = (result.stderr or result.stdout or "").strip() if capture else ""
        raise Bt02Error(
            f"Command failed with exit code {result.returncode}: {shlex.join(cmd)}"
            + (f"\n{detail}" if detail else "")
        )
    return result


def require_dependencies() -> None:
    required = ("virsh", "virt-install", "qemu-img", "socat", "sudo", "iptables", "pkill")
    missing = [
        name
        for name in required
        if subprocess.run(["sh", "-c", f"command -v {shlex.quote(name)} >/dev/null 2>&1"]).returncode != 0
    ]
    if missing:
        raise Bt02Error("Missing host dependencies: " + ", ".join(missing))
    for path in (VM_MANAGER, CHUTNEY_MANAGER):
        if not path.exists():
            raise Bt02Error(f"Required file does not exist: {path}")
    if ctypes.util.find_library("sodium") is None:
        raise Bt02Error("Host libsodium was not found; it is required for ciphertext verification")


def acquire_sudo() -> None:
    info("Acquiring sudo credentials...")
    run(["sudo", "-v"])


def repair_workspace_permissions() -> None:
    import grp
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


def _project_chutney_tor_pids() -> list[int]:
    runtime = str((PROJECT_ROOT / ".chutney" / "net").resolve())
    result = subprocess.run(["ps", "-eo", "pid=,comm=,args="], text=True, capture_output=True, check=True)
    pids: list[int] = []
    for raw_line in result.stdout.splitlines():
        parts = raw_line.strip().split(None, 2)
        if len(parts) != 3 or parts[1] != "tor" or runtime not in parts[2]:
            continue
        try:
            pids.append(int(parts[0]))
        except ValueError:
            pass
    return pids


def cleanup_stale_chutney_tor() -> None:
    pids = _project_chutney_tor_pids()
    if not pids:
        info("No stale project Chutney Tor processes found")
        return
    run(["sudo", "kill", "-TERM", *map(str, pids)], check=False)
    deadline = time.monotonic() + 5
    while time.monotonic() < deadline:
        if not _project_chutney_tor_pids():
            return
        time.sleep(0.25)
    pids = _project_chutney_tor_pids()
    if pids:
        run(["sudo", "kill", "-KILL", *map(str, pids)], check=False)


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
    info(f"Waiting for {vm_name} to finish booting and obtain DHCP...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ip = get_vm_ip_once(vm_name)
        if ip:
            ok(f"{vm_name} IP: {ip}")
            return ip
        time.sleep(2)
    raise Bt02Error(f"Timed out waiting for IP address of {vm_name}")


def start_chutney() -> None:
    run(["bash", str(CHUTNEY_MANAGER), "clean"], check=False)
    run(["bash", str(CHUTNEY_MANAGER), "start"])
    run(["bash", str(CHUTNEY_MANAGER), "status"])


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
            raise Bt02Error("SOCKS5 NoAuth rejected")
        sock.sendall(b"\x05\x01\x00\x03" + bytes([len(domain)]) + domain + struct.pack(">H", target_port))
        version, reply, reserved, atyp = recv_exact(sock, 4)
        if version != 5 or reserved != 0 or reply != 0:
            raise Bt02Error(f"SOCKS5 CONNECT failed: 0x{reply:02x}")
        if atyp == 1:
            recv_exact(sock, 4)
        elif atyp == 3:
            recv_exact(sock, recv_exact(sock, 1)[0])
        elif atyp == 4:
            recv_exact(sock, 16)
        else:
            raise Bt02Error("Unsupported SOCKS5 ATYP")
        recv_exact(sock, 2)
        return sock
    except Exception:
        sock.close()
        raise


def check_socks5() -> None:
    with socket.create_connection((TOR_SOCKS_HOST, TOR_SOCKS_PORT), timeout=5) as sock:
        sock.sendall(b"\x05\x01\x00")
        if recv_exact(sock, 2) != b"\x05\x00":
            raise Bt02Error("Chutney SOCKS5 is not ready")
    ok("Chutney SOCKS5 is ready")


def wait_for_hidden_service(onion: str, timeout: float = 60.0) -> None:
    info("Waiting for the hidden service through Chutney...")
    deadline = time.monotonic() + timeout
    attempt = 0
    while time.monotonic() < deadline:
        attempt += 1
        try:
            sock = socks5_connect(onion)
            try:
                header = recv_exact(sock, FRAME_HEADER_SIZE)
                action = header[0]
                length = struct.unpack("<I", header[17:21])[0]
                payload = recv_exact(sock, length) if length else b""
                if action == ACTION_AUTH_CHALLENGE and len(payload) == CONTROL_PAYLOAD_SIZE:
                    ok(f"Hidden service reachable (attempt {attempt})")
                    return
            finally:
                sock.close()
        except Exception:
            pass
        time.sleep(2)
    raise Bt02Error("Hidden service did not become reachable")


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
        raise Bt02Error(f"socat failed: {log_path.read_text(encoding='utf-8', errors='replace')}")
    ok("Started socat: " + " ".join(args))
    return process


def stop_process(process: subprocess.Popen[str] | None, name: str) -> None:
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
    child = pexpect.spawn("virsh", ["-c", "qemu:///system", "console", vm_name], encoding="utf-8", timeout=10)
    try:
        child.expect(r"Escape character is", timeout=20)
    except pexpect.TIMEOUT as exc:
        child.close(force=True)
        raise Bt02Error(f"Could not attach to {vm_name} serial console") from exc

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
            raise Bt02Error(f"Console for {vm_name} closed during login")
    else:
        child.close(force=True)
        raise Bt02Error(f"Timed out logging in to {vm_name}")

    child.send("stty -echo\r\n")
    child.expect(r"# ", timeout=10)
    child.send(f"export PS1='{SHELL_PROMPT}'\r\n")
    child.expect(re.escape(SHELL_PROMPT), timeout=10)
    ok(f"Logged into {vm_name} as root")
    return child


def console_cmd(child: pexpect.spawn, command: str, *, timeout: float = 30.0, check: bool = True) -> tuple[str, int]:
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
        raise Bt02Error(f"Command failed inside VM (rc={rc}): {command}")
    return output, rc


def upload_text(child: pexpect.spawn, remote_path: str, text: str) -> None:
    quoted_path = shlex.quote(remote_path)
    console_cmd(child, f": > {quoted_path}")
    for line in text.splitlines():
        console_cmd(child, f"printf '%s\\n' {shlex.quote(line)} >> {quoted_path}")


def remote_cat(child: pexpect.spawn, path: str) -> str:
    output, _ = console_cmd(child, f"cat {shlex.quote(path)} 2>/dev/null || true", timeout=60, check=False)
    return output


def configure_server(child: pexpect.spawn) -> None:
    config = '''[network]
listen_host = "0.0.0.0"
listen_port = 8080
tor_control_host = "192.168.122.1"
tor_control_port = 8006

[security]
memory_quota_percent = 80
max_messages_per_mailbox = 100000
'''
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
    raise Bt02Error("Timed out waiting for hidden-service address")


def configure_client(child: pexpect.spawn, onion: str, pfs_interval: int, cbr_interval_ms: int) -> None:
    config = f'''[network]
tor_socks_host = "{GATEWAY_IP}"
tor_socks_port = {TOR_SOCKS_PORT}

[relay]
onion_address = "{onion}"
onion_port = 80

[obfuscation]
mode = "cbr"
cbr_interval_ms = {cbr_interval_ms}
poisson_lambda = 5.0

[storage]
contacts_file_path = "{REMOTE_CONTACTS}"

[security]
pfs_message_interval = {pfs_interval}
'''
    upload_text(child, REMOTE_CLIENT_CONFIG, config)


def assert_client_binary_has_diagnostics(child: pexpect.spawn, vm_name: str) -> None:
    _, rc = console_cmd(
        child,
        "grep -q 'BT02_EVENT TEXT_CIPHERTEXT' /usr/bin/blank_chat_client 2>/dev/null",
        check=False,
    )
    if rc != 0:
        raise Bt02Error(
            f"{vm_name} does not contain BT-02 diagnostics. Run "
            "'./benchmarks/BT-02/run.sh --prepare' first."
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


def wait_client_log_count(child: pexpect.spawn, pattern: str, minimum: int, timeout: float = 30.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        if client_log_count(child, pattern) >= minimum:
            return
        time.sleep(0.2)
    raise Bt02Error(f"Timed out waiting for client log: {pattern}")


def wait_client_log(child: pexpect.spawn, pattern: str, timeout: float = 30.0) -> None:
    wait_client_log_count(child, pattern, 1, timeout)


def start_client_process(child: pexpect.spawn, vm_name: str) -> None:
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
    ok(f"blank_chat_client ready on {vm_name}")


def get_client_events(child: pexpect.spawn) -> list[dict[str, str]]:
    output, _ = console_cmd(
        child,
        f"grep 'BT02_EVENT' {REMOTE_CLIENT_LOG} 2>/dev/null || true",
        timeout=60,
        check=False,
    )
    events: list[dict[str, str]] = []
    for line in strip_ansi(output).splitlines():
        match = EVENT_RE.search(line)
        if not match:
            continue
        event_type, fields_text = match.groups()
        fields = {key: value for key, value in KV_RE.findall(fields_text)}
        fields["type"] = event_type
        events.append(fields)
    return events


def event_count(child: pexpect.spawn, event_type: str, fields: dict[str, str] | None = None) -> int:
    fields = fields or {}
    return sum(
        1
        for event in get_client_events(child)
        if event.get("type") == event_type
        and all(event.get(key) == value for key, value in fields.items())
    )


def wait_for_event(
    child: pexpect.spawn,
    event_type: str,
    after_count: int,
    fields: dict[str, str] | None = None,
    timeout: float = 30.0,
) -> dict[str, str]:
    fields = fields or {}
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        matches = [
            event
            for event in get_client_events(child)
            if event.get("type") == event_type
            and all(event.get(key) == value for key, value in fields.items())
        ]
        if len(matches) > after_count:
            return matches[-1]
        time.sleep(0.25)
    raise Bt02Error(f"Timed out waiting for BT02_EVENT {event_type} {fields}")


def extract_mnemonic(child: pexpect.spawn) -> str:
    before = client_log_count(child, "Your Identity Key (BIP39 Mnemonic)")
    client_command(child, "mykey")
    wait_client_log_count(child, "Your Identity Key (BIP39 Mnemonic)", before + 1, 10)
    output, _ = console_cmd(child, f"tail -n 80 {REMOTE_CLIENT_LOG}", check=False)
    matches = MNEMONIC_RE.findall(strip_ansi(output))
    if not matches:
        raise Bt02Error("Could not parse BIP39 mnemonic from client output")
    return matches[-1]


def add_contact(child: pexpect.spawn, alias: str, mnemonic: str) -> None:
    marker = f"Contact '{alias}' added successfully."
    before = client_log_count(child, marker)
    client_command(child, f"add {alias} {mnemonic}")
    wait_client_log_count(child, marker, before + 1, 15)


def connect_client(child: pexpect.spawn) -> None:
    before = client_log_count(child, "Successfully connected.")
    client_command(child, "connect")
    wait_client_log_count(child, "Successfully connected.", before + 1, 60)


def blake2b16(data: bytes) -> str:
    return hashlib.blake2b(data, digest_size=16).hexdigest()


def parse_contact_json(raw_json: str, alias: str) -> RawContactState:
    parsed = json.loads(raw_json)
    contacts = parsed.get("contacts")
    if not isinstance(contacts, list):
        raise Bt02Error("contacts.json has no contacts array")
    for item in contacts:
        if item.get("alias") != alias:
            continue
        tx_key = bytearray.fromhex(item["txKey"])
        rx_key = bytearray.fromhex(item["rxKey"])
        if len(tx_key) != KEY_SIZE or len(rx_key) != KEY_SIZE:
            raise Bt02Error("Unexpected key size in contacts.json")
        RAW_KEY_BUFFERS.extend((tx_key, rx_key))
        return RawContactState(
            tx_key=tx_key,
            rx_key=rx_key,
            tx_mailbox=item["txMailboxId"],
            rx_mailbox=item["rxMailboxId"],
            initial_pfs_complete=bool(item.get("initialPfsComplete", False)),
        )
    raise Bt02Error(f"Contact '{alias}' missing in contacts.json")


def read_contact(child: pexpect.spawn, alias: str, timeout: float = 10.0) -> RawContactState:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        raw = remote_cat(child, REMOTE_CONTACTS)
        if raw:
            try:
                return parse_contact_json(raw, alias)
            except (json.JSONDecodeError, KeyError, ValueError, Bt02Error):
                pass
        time.sleep(0.2)
    raise Bt02Error(f"Could not read stable contact '{alias}'")


def public_epoch(epoch: int, a: RawContactState, b: RawContactState) -> EpochState:
    return EpochState(epoch=epoch, a=a.public(), b=b.public())


def assert_directional_agreement(a: RawContactState, b: RawContactState) -> None:
    if not (
        bytes(a.tx_key) == bytes(b.rx_key)
        and bytes(a.rx_key) == bytes(b.tx_key)
        and a.tx_mailbox == b.rx_mailbox
        and a.rx_mailbox == b.tx_mailbox
    ):
        raise Bt02Error("A/B directional keys or MailboxID do not agree")


def assert_epoch_changed(
    old_a: RawContactState,
    old_b: RawContactState,
    new_a: RawContactState,
    new_b: RawContactState,
) -> None:
    if not all(
        (
            bytes(old_a.tx_key) != bytes(new_a.tx_key),
            bytes(old_a.rx_key) != bytes(new_a.rx_key),
            old_a.tx_mailbox != new_a.tx_mailbox,
            old_a.rx_mailbox != new_a.rx_mailbox,
            bytes(old_b.tx_key) != bytes(new_b.tx_key),
            bytes(old_b.rx_key) != bytes(new_b.rx_key),
            old_b.tx_mailbox != new_b.tx_mailbox,
            old_b.rx_mailbox != new_b.rx_mailbox,
        )
    ):
        raise Bt02Error("Not all session keys/MailboxID changed between epochs")


def same_state(left: RawContactState, right: RawContactState) -> bool:
    return (
        bytes(left.tx_key) == bytes(right.tx_key)
        and bytes(left.rx_key) == bytes(right.rx_key)
        and left.tx_mailbox == right.tx_mailbox
        and left.rx_mailbox == right.rx_mailbox
    )


def wait_for_initial_pfs(a: ClientHandle, b: ClientHandle, timeout: float = 45.0) -> tuple[RawContactState, RawContactState]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            a_state = read_contact(a.console, a.alias_for_peer, 2)
            b_state = read_contact(b.console, b.alias_for_peer, 2)
        except Bt02Error:
            time.sleep(0.2)
            continue
        if a_state.initial_pfs_complete and b_state.initial_pfs_complete:
            assert_directional_agreement(a_state, b_state)
            return a_state, b_state
        time.sleep(0.2)
    raise Bt02Error("Initial PFS did not complete on both clients")


def wait_for_epoch_change(
    a: ClientHandle,
    b: ClientHandle,
    old_a: RawContactState,
    old_b: RawContactState,
    timeout: float = 35.0,
) -> tuple[RawContactState, RawContactState]:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        try:
            new_a = read_contact(a.console, a.alias_for_peer, 2)
            new_b = read_contact(b.console, b.alias_for_peer, 2)
        except Bt02Error:
            time.sleep(0.2)
            continue
        if (
            new_a.initial_pfs_complete
            and new_b.initial_pfs_complete
            and (new_a.tx_mailbox != old_a.tx_mailbox or new_a.rx_mailbox != old_a.rx_mailbox)
        ):
            assert_epoch_changed(old_a, old_b, new_a, new_b)
            assert_directional_agreement(new_a, new_b)
            return new_a, new_b
        time.sleep(0.2)
    raise Bt02Error("Timed out waiting for next PFS epoch")


def message_id(message: str) -> str:
    return blake2b16(message.encode())


def send_message(
    sender: ClientHandle,
    receiver: ClientHandle,
    message: str,
    capture_ciphertext: bool = False,
    timeout: float = 40.0,
) -> tuple[bytes | None, str]:
    msg_id = message_id(message)
    cipher_before = event_count(sender.console, "TEXT_CIPHERTEXT", {"msg_id": msg_id})
    ack_before = event_count(sender.console, "ACK_DELIVERED", {"msg_id": msg_id})
    receive_marker = f"New message from {sender.local_alias_seen_by_peer}: {message}"
    receive_before = client_log_count(receiver.console, receive_marker)

    client_command(sender.console, f"send {sender.alias_for_peer} {message}")

    cipher_event = wait_for_event(
        sender.console,
        "TEXT_CIPHERTEXT",
        cipher_before,
        {"msg_id": msg_id},
        timeout,
    )
    wait_client_log_count(receiver.console, receive_marker, receive_before + 1, timeout)
    wait_for_event(sender.console, "ACK_DELIVERED", ack_before, {"msg_id": msg_id}, timeout)

    mailbox = cipher_event.get("mailbox", "")
    ciphertext: bytes | None = None
    if capture_ciphertext:
        ciphertext_hex = cipher_event.get("ciphertext")
        if not ciphertext_hex:
            raise Bt02Error("Missing ciphertext in diagnostic event")
        ciphertext = bytes.fromhex(ciphertext_hex)

    ok(f"{sender.name}->{receiver.name}: delivered and ACKed {message}")
    return ciphertext, mailbox


class SodiumVerifier:
    def __init__(self) -> None:
        library = ctypes.util.find_library("sodium")
        if not library:
            raise Bt02Error("libsodium not found")
        self.lib = ctypes.cdll.LoadLibrary(library)
        if self.lib.sodium_init() < 0:
            raise Bt02Error("sodium_init failed")
        self.decrypt_fn = self.lib.crypto_aead_xchacha20poly1305_ietf_decrypt
        self.decrypt_fn.restype = ctypes.c_int

    def decrypt_and_unpad(self, key: bytes, ciphertext: bytes) -> bytes | None:
        if len(key) != KEY_SIZE or len(ciphertext) < NONCE_SIZE + MAC_SIZE:
            return None
        nonce = ciphertext[:NONCE_SIZE]
        encrypted = ciphertext[NONCE_SIZE:]
        capacity = len(encrypted) - MAC_SIZE
        plaintext = (ctypes.c_ubyte * capacity)()
        plaintext_len = ctypes.c_ulonglong(0)
        encrypted_buf = (ctypes.c_ubyte * len(encrypted)).from_buffer_copy(encrypted)
        nonce_buf = (ctypes.c_ubyte * NONCE_SIZE).from_buffer_copy(nonce)
        key_buf = (ctypes.c_ubyte * KEY_SIZE).from_buffer_copy(key)
        rc = self.decrypt_fn(
            plaintext,
            ctypes.byref(plaintext_len),
            None,
            encrypted_buf,
            ctypes.c_ulonglong(len(encrypted)),
            None,
            ctypes.c_ulonglong(0),
            nonce_buf,
            key_buf,
        )
        if rc != 0:
            return None
        data = bytes(plaintext[: plaintext_len.value])
        index = len(data)
        while index > 0 and data[index - 1] == 0:
            index -= 1
        if index == 0 or data[index - 1] != PADDING_MARKER:
            return None
        return data[: index - 1]


def verify_ciphertext_separation(
    verifier: SodiumVerifier,
    old_message: str,
    new_message: str,
    old_key: bytearray,
    new_key: bytearray,
    old_ciphertext: bytes,
    new_ciphertext: bytes,
) -> tuple[bool, bool, bool, bool]:
    old_expected = bytes([TEXT_MESSAGE_OPCODE]) + old_message.encode()
    new_expected = bytes([TEXT_MESSAGE_OPCODE]) + new_message.encode()
    old_with_old = verifier.decrypt_and_unpad(bytes(old_key), old_ciphertext)
    old_with_new = verifier.decrypt_and_unpad(bytes(new_key), old_ciphertext)
    new_with_new = verifier.decrypt_and_unpad(bytes(new_key), new_ciphertext)
    new_with_old = verifier.decrypt_and_unpad(bytes(old_key), new_ciphertext)
    return (
        old_with_old == old_expected,
        old_with_new is not None,
        new_with_new == new_expected,
        new_with_old is not None,
    )


def perform_rotation(
    rotation_no: int,
    sender: ClientHandle,
    receiver: ClientHandle,
    a: ClientHandle,
    b: ClientHandle,
    old_a: RawContactState,
    old_b: RawContactState,
    pfs_interval: int,
    verifier: SodiumVerifier,
) -> tuple[RawContactState, RawContactState, EpochState, RotationEvidence]:
    old_sender = old_a if sender.name == "A" else old_b
    old_receiver = old_b if sender.name == "A" else old_a
    epoch_no = rotation_no - 1

    info(f"Rotation {rotation_no}: preparing threshold with initiator {sender.name}")
    threshold_before = client_log_count(sender.console, "PFS threshold reached. Initiating background key rotation...")

    for i in range(1, pfs_interval + 1):
        send_message(sender, receiver, f"BT02_R{rotation_no}_PREP_{i}")
        current_sender = read_contact(sender.console, sender.alias_for_peer)
        current_receiver = read_contact(receiver.console, receiver.alias_for_peer)
        if not same_state(current_sender, old_sender) or not same_state(current_receiver, old_receiver):
            raise Bt02Error(f"Rotation {rotation_no} occurred before trigger send")

    if client_log_count(sender.console, "PFS threshold reached. Initiating background key rotation...") != threshold_before:
        raise Bt02Error("Periodic PFS started prematurely")
    ok(f"rotation {rotation_no}: no rotation during first {pfs_interval} sends")

    init_eph_before = event_count(sender.console, "PFS_LOCAL_EPHEMERAL", {"role": "initiator"})
    req_before = event_count(receiver.console, "PFS_ROTATE_REQUEST_VERIFIED")
    resp_eph_before = event_count(receiver.console, "PFS_LOCAL_EPHEMERAL", {"role": "responder"})
    retained_before = event_count(receiver.console, "OLD_RX_RETAINED")
    old_accept_before = event_count(receiver.console, "OLD_RX_MESSAGE_ACCEPTED")
    ack_before = event_count(sender.console, "PFS_ROTATE_ACK_VERIFIED")

    old_message = f"BT02_R{rotation_no}_E{epoch_no}_TRIGGER"
    old_ciphertext, old_mailbox = send_message(sender, receiver, old_message, True)
    if old_ciphertext is None:
        raise Bt02Error("Missing old-epoch trigger ciphertext")

    wait_client_log_count(
        sender.console,
        "PFS threshold reached. Initiating background key rotation...",
        threshold_before + 1,
        20,
    )
    init_event = wait_for_event(sender.console, "PFS_LOCAL_EPHEMERAL", init_eph_before, {"role": "initiator"}, 20)
    wait_for_event(receiver.console, "PFS_ROTATE_REQUEST_VERIFIED", req_before, timeout=20)
    resp_event = wait_for_event(receiver.console, "PFS_LOCAL_EPHEMERAL", resp_eph_before, {"role": "responder"}, 20)
    retained = wait_for_event(receiver.console, "OLD_RX_RETAINED", retained_before, timeout=20)
    old_accepted = wait_for_event(receiver.console, "OLD_RX_MESSAGE_ACCEPTED", old_accept_before, timeout=20)
    wait_for_event(sender.console, "PFS_ROTATE_ACK_VERIFIED", ack_before, timeout=20)

    if old_mailbox != old_sender.tx_mailbox:
        raise Bt02Error("Trigger text did not use the old TX mailbox")
    if retained.get("old_mailbox") != old_receiver.rx_mailbox:
        raise Bt02Error("Responder retained the wrong old RX mailbox")
    if old_accepted.get("mailbox") != old_receiver.rx_mailbox:
        raise Bt02Error("Trigger text was not accepted through retained old RX mailbox")

    new_a, new_b = wait_for_epoch_change(a, b, old_a, old_b)
    new_sender = new_a if sender.name == "A" else new_b
    new_receiver = new_b if sender.name == "A" else new_a

    retired_before = event_count(receiver.console, "OLD_RX_RETIRED")
    new_message = f"BT02_R{rotation_no}_E{epoch_no + 1}_CONFIRM"
    new_ciphertext, new_mailbox = send_message(sender, receiver, new_message, True)
    if new_ciphertext is None:
        raise Bt02Error("Missing new-epoch confirmation ciphertext")
    retired = wait_for_event(receiver.console, "OLD_RX_RETIRED", retired_before, timeout=20)

    if new_mailbox != new_sender.tx_mailbox:
        raise Bt02Error("Confirmation text did not use the new TX mailbox")
    if retired.get("old_mailbox") != old_receiver.rx_mailbox:
        raise Bt02Error("Retired RX mailbox does not match previous epoch")
    if retired.get("new_mailbox") != new_receiver.rx_mailbox:
        raise Bt02Error("Retirement event does not identify current RX mailbox")

    old_ok, old_with_new, new_ok, new_with_old = verify_ciphertext_separation(
        verifier,
        old_message,
        new_message,
        old_sender.tx_key,
        new_sender.tx_key,
        old_ciphertext,
        new_ciphertext,
    )
    if not old_ok or old_with_new or not new_ok or new_with_old:
        raise Bt02Error("Cross-epoch ciphertext separation check failed")

    assert_epoch_changed(old_a, old_b, new_a, new_b)
    assert_directional_agreement(new_a, new_b)

    evidence = RotationEvidence(
        rotation=rotation_no,
        initiator=sender.name,
        responder=receiver.name,
        old_epoch=epoch_no,
        new_epoch=epoch_no + 1,
        initiator_ephemeral_fingerprint=init_event["fp"],
        responder_ephemeral_fingerprint=resp_event["fp"],
        request_signature_verified=True,
        ack_signature_verified=True,
        old_rx_retained=True,
        old_rx_message_accepted=True,
        old_rx_retired=True,
        old_ciphertext_with_old_key=old_ok,
        old_ciphertext_with_new_key=old_with_new,
        new_ciphertext_with_new_key=new_ok,
        new_ciphertext_with_old_key=new_with_old,
        old_ciphertext_fingerprint=blake2b16(old_ciphertext),
        new_ciphertext_fingerprint=blake2b16(new_ciphertext),
    )

    ok(f"rotation {rotation_no}: signed request/ACK observed")
    ok(f"rotation {rotation_no}: keys and MailboxID changed and A/B agree")
    ok(f"rotation {rotation_no}: old-mailbox in-flight message delivered")
    ok(f"rotation {rotation_no}: old RX state retired only after new-mailbox traffic")
    ok(f"rotation {rotation_no}: cross-epoch ciphertext separation verified")

    return new_a, new_b, public_epoch(epoch_no + 1, new_a, new_b), evidence


def stop_client(child: pexpect.spawn) -> None:
    try:
        client_command(child, "exit")
        time.sleep(0.5)
    except Exception:
        pass
    console_cmd(child, f"kill $(cat {REMOTE_CLIENT_PID} 2>/dev/null) 2>/dev/null || true", check=False)
    console_cmd(child, "exec 3>&- 2>/dev/null || true", check=False)


def stop_server(child: pexpect.spawn) -> None:
    console_cmd(child, f"kill $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null || true", check=False)


def collect_filtered_client_log(child: pexpect.spawn, destination: Path) -> None:
    output, _ = console_cmd(
        child,
        "grep -E 'BT02_EVENT|PFS|New message from|DELIVERED|Successfully connected|Contact .* added successfully' "
        f"{REMOTE_CLIENT_LOG} 2>/dev/null || true",
        timeout=60,
        check=False,
    )
    cleaned = strip_ansi(output)
    cleaned = re.sub(r"(ciphertext=)[0-9a-fA-F]+", r"\1<redacted>", cleaned)
    destination.write_text(cleaned + ("\n" if cleaned else ""), encoding="utf-8")


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


def wipe_raw_keys() -> None:
    for buffer in RAW_KEY_BUFFERS:
        for i in range(len(buffer)):
            buffer[i] = 0
    RAW_KEY_BUFFERS.clear()


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BT-02 VM-based periodic PFS integration test")
    parser.add_argument("--pfs-interval", type=int, default=2)
    parser.add_argument("--cbr-interval-ms", type=int, default=250)
    parser.add_argument("--rotations", type=int, default=3)
    parser.add_argument("--keep-environment", action="store_true")
    args = parser.parse_args()
    if args.pfs_interval < 1:
        parser.error("--pfs-interval must be >= 1")
    if args.cbr_interval_ms < 50:
        parser.error("--cbr-interval-ms must be >= 50")
    if args.rotations < 3:
        parser.error("--rotations must be >= 3")
    return args


def main() -> int:
    global RUN_LOG
    args = parse_args()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_dir = BENCHMARKS_DIR / "results" / "BT-02" / stamp
    result_dir.mkdir(parents=True, exist_ok=True)
    RUN_LOG = result_dir / "orchestrator.log"

    commit = subprocess.run(["git", "rev-parse", "HEAD"], cwd=PROJECT_ROOT, text=True, capture_output=True).stdout.strip()
    status = subprocess.run(["git", "status", "--short"], cwd=PROJECT_ROOT, text=True, capture_output=True).stdout
    metadata = RunMetadata(
        started_at=datetime.now().astimezone().isoformat(),
        commit=commit,
        dirty_worktree=bool(status.strip()),
        pfs_message_interval=args.pfs_interval,
        cbr_interval_ms=args.cbr_interval_ms,
        rotations_requested=args.rotations,
    )
    (result_dir / "commit.txt").write_text(commit + "\n", encoding="utf-8")
    (result_dir / "git_status.txt").write_text(status, encoding="utf-8")

    server_console = None
    client_a_console = None
    client_b_console = None
    control_socat = None
    service_socat = None
    socks_socat = None
    firewall_added = False
    chutney_started = False
    epochs: list[EpochState] = []
    rotations: list[RotationEvidence] = []
    exit_code = 1

    try:
        require_dependencies()
        acquire_sudo()
        repair_workspace_permissions()
        cleanup_stale_chutney_tor()
        recreate_vms()

        metadata.server_ip = wait_for_vm_ip(SERVER_VM)
        metadata.client_a_ip = wait_for_vm_ip(CLIENT_A_VM)
        metadata.client_b_ip = wait_for_vm_ip(CLIENT_B_VM)

        start_chutney()
        chutney_started = True
        check_socks5()
        firewall_added = ensure_firewall_rule()
        stop_stale_socat()

        control_socat = start_socat(
            [f"TCP-LISTEN:{TOR_CONTROL_PORT},bind={GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{TOR_CONTROL_PORT}"],
            result_dir / "socat_control.log",
        )
        service_socat = start_socat(
            [f"TCP-LISTEN:{SERVER_PORT},bind=127.0.0.1,fork,reuseaddr", f"TCP:{metadata.server_ip}:{SERVER_PORT}"],
            result_dir / "socat_service.log",
        )
        socks_socat = start_socat(
            [f"TCP-LISTEN:{TOR_SOCKS_PORT},bind={GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{TOR_SOCKS_PORT}"],
            result_dir / "socat_socks.log",
        )

        server_console = login_to_console(SERVER_VM)
        configure_server(server_console)
        start_server(server_console)
        metadata.onion = wait_for_onion(server_console)
        wait_for_hidden_service(metadata.onion)

        client_a_console = login_to_console(CLIENT_A_VM)
        client_b_console = login_to_console(CLIENT_B_VM)
        assert_client_binary_has_diagnostics(client_a_console, CLIENT_A_VM)
        assert_client_binary_has_diagnostics(client_b_console, CLIENT_B_VM)

        configure_client(client_a_console, metadata.onion, args.pfs_interval, args.cbr_interval_ms)
        configure_client(client_b_console, metadata.onion, args.pfs_interval, args.cbr_interval_ms)
        start_client_process(client_a_console, CLIENT_A_VM)
        start_client_process(client_b_console, CLIENT_B_VM)

        mnemonic_a = extract_mnemonic(client_a_console)
        mnemonic_b = extract_mnemonic(client_b_console)
        add_contact(client_a_console, "bob", mnemonic_b)
        add_contact(client_b_console, "alice", mnemonic_a)
        ok("Mutual contacts created through real client CLI")

        connect_client(client_a_console)
        connect_client(client_b_console)
        ok("Both real clients connected through Chutney to the .onion service")

        a = ClientHandle("A", CLIENT_A_VM, "bob", "alice", client_a_console)
        b = ClientHandle("B", CLIENT_B_VM, "alice", "bob", client_b_console)
        a_state, b_state = wait_for_initial_pfs(a, b)
        epochs.append(public_epoch(0, a_state, b_state))
        ok("initial PFS completed; epoch 0 state agrees between A and B")

        verifier = SodiumVerifier()
        seen_ephemeral: set[str] = set()

        for rotation_no in range(1, args.rotations + 1):
            sender, receiver = (a, b) if rotation_no % 2 == 1 else (b, a)
            a_state, b_state, epoch, evidence = perform_rotation(
                rotation_no,
                sender,
                receiver,
                a,
                b,
                a_state,
                b_state,
                args.pfs_interval,
                verifier,
            )
            for fp in (evidence.initiator_ephemeral_fingerprint, evidence.responder_ephemeral_fingerprint):
                if fp in seen_ephemeral:
                    raise Bt02Error("Ephemeral public-key fingerprint was reused")
                seen_ephemeral.add(fp)
            epochs.append(epoch)
            rotations.append(evidence)
            metadata.rotations_completed = rotation_no
            ok(f"rotation {rotation_no}: fresh ephemeral key fingerprints confirmed")

        if len(epochs) < 4:
            raise Bt02Error("Expected at least epochs 0..3")

        metadata.result = "passed"
        exit_code = 0
        ok("BT-02 completed successfully")
        print("\nBT-02: PASS", flush=True)
        _write_log("BT-02: PASS")

    except KeyboardInterrupt:
        metadata.result = "interrupted"
        exit_code = 130
        warn("Interrupted by user")
    except Exception as exc:
        metadata.result = "failed"
        exit_code = 1
        warn(str(exc))
    finally:
        if client_a_console is not None:
            try:
                collect_filtered_client_log(client_a_console, result_dir / "client_a_events.log")
            except Exception as exc:
                warn(f"Could not collect client A events: {exc}")
        if client_b_console is not None:
            try:
                collect_filtered_client_log(client_b_console, result_dir / "client_b_events.log")
            except Exception as exc:
                warn(f"Could not collect client B events: {exc}")
        if server_console is not None:
            try:
                server_log = remote_cat(server_console, REMOTE_SERVER_LOG)
                (result_dir / "server.log").write_text(server_log + ("\n" if server_log else ""), encoding="utf-8")
            except Exception as exc:
                warn(f"Could not collect server log: {exc}")

        for child in (client_a_console, client_b_console):
            if child is not None:
                try:
                    stop_client(child)
                except Exception:
                    pass
        if server_console is not None:
            try:
                stop_server(server_console)
            except Exception:
                pass

        close_console(client_a_console)
        close_console(client_b_console)
        close_console(server_console)
        stop_process(socks_socat, "VM-to-Chutney SOCKS socat")
        stop_process(service_socat, "hidden-service socat")
        stop_process(control_socat, "control-port socat")
        remove_firewall_rule_if_added(firewall_added)

        if not args.keep_environment:
            if chutney_started:
                subprocess.run(["bash", str(CHUTNEY_MANAGER), "stop"], cwd=PROJECT_ROOT, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            destroy_vms()
        else:
            info("Keeping VMs and Chutney running")

        (result_dir / "epochs.json").write_text(json.dumps([asdict(e) for e in epochs], indent=2) + "\n", encoding="utf-8")
        (result_dir / "rotations.json").write_text(json.dumps([asdict(r) for r in rotations], indent=2) + "\n", encoding="utf-8")
        (result_dir / "run.json").write_text(json.dumps(asdict(metadata), indent=2) + "\n", encoding="utf-8")
        wipe_raw_keys()
        info(f"Results directory: {result_dir}")

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
