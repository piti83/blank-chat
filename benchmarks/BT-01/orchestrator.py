#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
import shlex
import socket
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
TEST_DIR = BENCHMARKS_DIR / "BT-01"
VM_MANAGER = BENCHMARKS_DIR / "vm_manager.py"
CHUTNEY_MANAGER = BENCHMARKS_DIR / "chutney_manager.sh"
AUTH_TEST = TEST_DIR / "auth_test.py"
SERVER_VM = "bc-server"
CLIENT_VMS = ("bc-client-1", "bc-client-2")
ALL_VMS = (SERVER_VM, *CLIENT_VMS)
GATEWAY_IP = "192.168.122.1"
TOR_CONTROL_PORT = 8006
TOR_SOCKS_HOST = "127.0.0.1"
TOR_SOCKS_PORT = 9050
SERVER_PORT = 8080
REMOTE_SERVER_CONFIG = "/etc/blank-chat/server_config.toml"
REMOTE_SERVER_LOG = "/tmp/bt01_server.log"
REMOTE_SERVER_PID = "/tmp/bt01_server.pid"
PROMPT = "__BC_BT01_PROMPT__ "

class BenchmarkError(RuntimeError):
    pass

@dataclass
class RunMetadata:
    started_at: str
    commit: str = ""
    git_dirty: bool = False
    server_vm: str = SERVER_VM
    server_ip: str = ""
    onion: str = ""
    auth_test_exit_code: int | None = None
    result: str = "incomplete"

def info(message: str) -> None:
    print(f"[i] {message}", flush=True)

def ok(message: str) -> None:
    print(f"[+] {message}", flush=True)

def warn(message: str) -> None:
    print(f"[!] {message}", file=sys.stderr, flush=True)

def run(cmd: list[str], *, check: bool = True, capture: bool = False, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    info("$ " + shlex.join(cmd))
    result = subprocess.run(cmd, cwd=str(cwd or PROJECT_ROOT), text=True, capture_output=capture)
    if check and result.returncode != 0:
        detail = ""
        if capture:
            detail = (result.stderr or result.stdout or "").strip()
        raise BenchmarkError(
            f"Command failed with exit code {result.returncode}: {shlex.join(cmd)}"
            + (f"\n{detail}" if detail else "")
        )
    return result

def require_dependencies() -> None:
    required = ("virsh", "virt-install", "qemu-img", "socat", "sudo", "iptables", "pkill", "git")
    missing = [
        name
        for name in required
        if subprocess.run(["sh", "-c", f"command -v {shlex.quote(name)} >/dev/null 2>&1"]).returncode != 0
    ]
    if missing:
        raise BenchmarkError("Missing host dependencies: " + ", ".join(missing))
    for path in (VM_MANAGER, CHUTNEY_MANAGER, AUTH_TEST):
        if not path.exists():
            raise BenchmarkError(f"Required file does not exist: {path}")

def acquire_sudo() -> None:
    info("Acquiring sudo credentials for firewall setup...")
    run(["sudo", "-v"])
    ok("sudo credentials ready")

def repair_workspace_permissions() -> None:
    import grp
    import os
    import pwd

    uid = os.getuid()
    gid = os.getgid()
    user = pwd.getpwuid(uid).pw_name
    group = grp.getgrgid(gid).gr_name
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
            info(f"Repairing ownership of {path}...")
            run(["sudo", "chown", "-R", f"{user}:{group}", str(path)])
            ok(f"Ownership repaired: {path}")

def git_state() -> tuple[str, bool]:
    commit = subprocess.run(
        ["git", "rev-parse", "HEAD"], cwd=str(PROJECT_ROOT), text=True, capture_output=True, check=True
    ).stdout.strip()
    dirty = bool(
        subprocess.run(
            ["git", "status", "--porcelain"], cwd=str(PROJECT_ROOT), text=True, capture_output=True, check=True
        ).stdout.strip()
    )
    return commit, dirty

def _project_chutney_tor_pids() -> list[int]:
    runtime = str((PROJECT_ROOT / ".chutney" / "net").resolve())
    result = subprocess.run(["ps", "-eo", "pid=,comm=,args="], text=True, capture_output=True, check=True)
    pids: list[int] = []
    for raw_line in result.stdout.splitlines():
        line = raw_line.strip()
        if not line:
            continue
        parts = line.split(None, 2)
        if len(parts) < 3:
            continue
        pid_text, comm, args = parts
        if comm != "tor" or runtime not in args:
            continue
        try:
            pids.append(int(pid_text))
        except ValueError:
            continue
    return pids

def cleanup_stale_chutney_tor() -> None:
    pids = _project_chutney_tor_pids()
    if not pids:
        info("No stale project Chutney Tor processes found")
        return
    warn("Found stale project Chutney Tor processes: " + ", ".join(str(pid) for pid in pids))
    run(["sudo", "kill", "-TERM", *[str(pid) for pid in pids]], check=False)
    deadline = time.monotonic() + 5.0
    while time.monotonic() < deadline:
        remaining = _project_chutney_tor_pids()
        if not remaining:
            ok("Stale Chutney Tor processes stopped")
            return
        time.sleep(0.25)
    remaining = _project_chutney_tor_pids()
    if remaining:
        run(["sudo", "kill", "-KILL", *[str(pid) for pid in remaining]], check=False)
        time.sleep(0.5)
    remaining = _project_chutney_tor_pids()
    if remaining:
        raise BenchmarkError("Could not stop stale project Chutney Tor processes: " + ", ".join(str(pid) for pid in remaining))
    ok("Stale Chutney Tor processes killed")

def recreate_vms() -> None:
    info("Recreating benchmark VMs...")
    run([sys.executable, str(VM_MANAGER)])
    ok("VMs started")

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

def wait_for_vm_ip(vm_name: str, timeout: float = 120.0) -> str:
    info(f"Waiting for DHCP address of {vm_name}...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ip = get_vm_ip_once(vm_name)
        if ip:
            ok(f"{vm_name} IP: {ip}")
            return ip
        time.sleep(2)
    raise BenchmarkError(f"Timed out waiting for IP address of {vm_name}")

def start_chutney() -> None:
    info("Resetting private Chutney network...")
    run(["bash", str(CHUTNEY_MANAGER), "clean"], check=False)
    run(["bash", str(CHUTNEY_MANAGER), "start"])
    run(["bash", str(CHUTNEY_MANAGER), "status"])
    ok("Chutney network is bootstrapped and running")

def check_socks5(timeout: float = 5.0) -> None:
    info(f"Checking SOCKS5 at {TOR_SOCKS_HOST}:{TOR_SOCKS_PORT}...")
    with socket.create_connection((TOR_SOCKS_HOST, TOR_SOCKS_PORT), timeout=timeout) as sock:
        sock.settimeout(timeout)
        sock.sendall(b"\x05\x01\x00")
        response = sock.recv(2)
        if response != b"\x05\x00":
            raise BenchmarkError(f"Unexpected SOCKS5 greeting response: {response!r}")
    ok("SOCKS5 is accepting NoAuth connections")

def ensure_firewall_rule() -> bool:
    check = subprocess.run(
        ["sudo", "iptables", "-C", "INPUT", "-i", "virbr0", "-j", "ACCEPT"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    if check.returncode == 0:
        info("iptables rule for virbr0 already exists")
        return False
    run(["sudo", "iptables", "-I", "INPUT", "-i", "virbr0", "-j", "ACCEPT"])
    ok("Inserted temporary iptables rule for virbr0")
    return True

def remove_firewall_rule_if_added(added: bool) -> None:
    if not added:
        return
    subprocess.run(
        ["sudo", "iptables", "-D", "INPUT", "-i", "virbr0", "-j", "ACCEPT"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    info("Removed temporary iptables rule")

def stop_stale_socat() -> None:
    patterns = (
        r"socat TCP-LISTEN:8006,bind=192\.168\.122\.1",
        r"socat TCP-LISTEN:8080,bind=127\.0\.0\.1",
        r"socat TCP-LISTEN:9050,bind=192\.168\.122\.1",
    )
    for pattern in patterns:
        subprocess.run(["pkill", "-f", pattern], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(0.3)

def start_socat(args: list[str], log_path: Path) -> subprocess.Popen[str]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(["socat", *args], stdout=log_file, stderr=subprocess.STDOUT, text=True)
    log_file.close()
    time.sleep(0.5)
    if proc.poll() is not None:
        detail = log_path.read_text(encoding="utf-8", errors="replace")
        raise BenchmarkError(f"socat failed to start:\n{detail}")
    ok("Started socat: " + " ".join(args))
    return proc

def stop_process(proc: subprocess.Popen[str] | None, name: str) -> None:
    if proc is None or proc.poll() is not None:
        return
    info(f"Stopping {name}...")
    proc.terminate()
    try:
        proc.wait(timeout=3)
    except subprocess.TimeoutExpired:
        proc.kill()
        proc.wait(timeout=2)

def login_to_console(vm_name: str, timeout: float = 120.0) -> pexpect.spawn:
    info(f"Opening serial console for {vm_name}...")
    child = pexpect.spawn("virsh", ["-c", "qemu:///system", "console", vm_name], encoding="utf-8", timeout=10)
    try:
        child.expect(r"Escape character is", timeout=20)
    except pexpect.TIMEOUT as exc:
        child.close(force=True)
        raise BenchmarkError(f"Could not attach to {vm_name} serial console") from exc
    deadline = time.monotonic() + timeout
    logged_in = False
    while time.monotonic() < deadline:
        child.send("\r\n")
        idx = child.expect([r"(?i)login:", r"root@[^#\r\n]*#", r"# ", pexpect.TIMEOUT, pexpect.EOF], timeout=3)
        if idx == 0:
            child.send("root\r\n")
            continue
        if idx in (1, 2):
            logged_in = True
            break
        if idx == 4:
            break
    if not logged_in:
        child.close(force=True)
        raise BenchmarkError(f"Timed out logging in to {vm_name}")
    child.send("stty -echo\r\n")
    child.expect(r"# ", timeout=10)
    child.send(f"export PS1='{PROMPT}'\r\n")
    child.expect(re.escape(PROMPT), timeout=10)
    ok(f"Logged into {vm_name} as root")
    return child

def console_cmd(child: pexpect.spawn, command: str, *, timeout: float = 30.0, check: bool = True) -> tuple[str, int]:
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
    end_pattern = re.escape(end_marker) + r":(\d+)"
    child.expect(end_pattern, timeout=timeout)
    output = child.before.replace("\r", "").strip()
    rc = int(child.match.group(1))
    child.expect(re.escape(PROMPT), timeout=10)
    if check and rc != 0:
        raise BenchmarkError(f"Command failed inside VM (rc={rc}): {command}\n{output}")
    return output, rc

def upload_text(child: pexpect.spawn, remote_path: str, text: str) -> None:
    qpath = shlex.quote(remote_path)
    console_cmd(child, f": > {qpath}")
    for line in text.splitlines():
        console_cmd(child, f"printf '%s\\n' {shlex.quote(line)} >> {qpath}")

def configure_server(child: pexpect.spawn) -> None:
    info("Writing BT-01 server configuration...")
    config = """[network]
listen_host = "0.0.0.0"
listen_port = 8080
tor_control_host = "192.168.122.1"
tor_control_port = 8006

[security]
memory_quota_percent = 80
max_messages_per_mailbox = 5000000
"""
    console_cmd(child, "mkdir -p /etc/blank-chat")
    upload_text(child, REMOTE_SERVER_CONFIG, config)
    ok("Server configuration written")

def start_server(child: pexpect.spawn) -> None:
    info("Starting blank_chat_server...")
    console_cmd(child, "killall blank_chat_server 2>/dev/null || true", check=False)
    console_cmd(child, f"rm -f {REMOTE_SERVER_LOG} {REMOTE_SERVER_PID}")
    console_cmd(
        child,
        "cd /etc/blank-chat && "
        f"blank_chat_server > {REMOTE_SERVER_LOG} 2>&1 & "
        f"echo $! > {REMOTE_SERVER_PID}",
    )
    time.sleep(1)

def wait_for_onion(child: pexpect.spawn, timeout: float = 60.0) -> str:
    info("Waiting for hidden-service address...")
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
        _, alive_rc = console_cmd(
            child,
            f"kill -0 $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null",
            check=False,
        )
        if alive_rc != 0:
            log_text, _ = console_cmd(child, f"cat {REMOTE_SERVER_LOG} 2>/dev/null || true", check=False)
            raise BenchmarkError("blank_chat_server exited before publishing onion address:\n" + log_text)
        time.sleep(1)
    log_text, _ = console_cmd(child, f"cat {REMOTE_SERVER_LOG} 2>/dev/null || true", check=False)
    raise BenchmarkError(f"Timed out waiting for onion address.\nServer log:\n{log_text}")

def wait_for_hidden_service(onion: str, timeout: float = 60.0) -> None:
    info("Waiting until the hidden service is reachable end-to-end...")
    deadline = time.monotonic() + timeout
    attempt = 0
    while time.monotonic() < deadline:
        attempt += 1
        result = subprocess.run(
            [sys.executable, str(AUTH_TEST), "--onion", onion, "--probe-only", "--timeout", "5"],
            cwd=str(PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode == 0:
            ok(f"Hidden service is reachable (attempt {attempt})")
            return
        time.sleep(2)
    raise BenchmarkError(f"Hidden service {onion} did not become reachable within {timeout:.0f}s")

def remote_cat(child: pexpect.spawn, path: str) -> str:
    output, _ = console_cmd(child, f"cat {shlex.quote(path)} 2>/dev/null || true", timeout=60, check=False)
    return output

def save_server_log(child: pexpect.spawn, result_dir: Path) -> None:
    server_log = remote_cat(child, REMOTE_SERVER_LOG)
    (result_dir / "server.log").write_text(
        server_log + ("\n" if server_log and not server_log.endswith("\n") else ""),
        encoding="utf-8",
    )

def run_auth_test(onion: str, result_dir: Path, timeout: float) -> int:
    summary_path = result_dir / "auth_test.json"
    log_path = result_dir / "auth_test.log"
    cmd = [
        sys.executable,
        str(AUTH_TEST),
        "--onion",
        onion,
        "--timeout",
        str(timeout),
        "--summary-json",
        str(summary_path),
    ]
    info("Starting BT-01 protocol test...")
    info("$ " + shlex.join(cmd))
    result = subprocess.run(cmd, cwd=str(PROJECT_ROOT), text=True, capture_output=True)
    combined = result.stdout + (result.stderr or "")
    log_path.write_text(combined, encoding="utf-8")
    if result.stdout:
        print(result.stdout, end="")
    if result.stderr:
        print(result.stderr, end="", file=sys.stderr)
    return result.returncode

def stop_server(child: pexpect.spawn) -> None:
    console_cmd(child, f"kill $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null || true", check=False)

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
            subprocess.run(
                ["virsh", "-c", "qemu:///system", "destroy", vm],
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL,
            )
        subprocess.run(
            ["virsh", "-c", "qemu:///system", "undefine", vm, "--nvram", "--remove-all-storage"],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )

def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description="BT-01: Challenge-Response + PoW access-control test")
    parser.add_argument("--auth-timeout", type=float, default=15.0, help="socket timeout used by auth_test.py")
    parser.add_argument(
        "--keep-environment",
        action="store_true",
        help="leave VMs and Chutney running after the run for debugging",
    )
    args = parser.parse_args()
    if args.auth_timeout <= 0:
        parser.error("--auth-timeout must be > 0")
    return args

def main() -> int:
    args = parse_args()
    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_dir = BENCHMARKS_DIR / "results" / "BT-01" / stamp
    result_dir.mkdir(parents=True, exist_ok=True)
    metadata = RunMetadata(started_at=datetime.now().astimezone().isoformat())
    console: pexpect.spawn | None = None
    control_socat: subprocess.Popen[str] | None = None
    service_socat: subprocess.Popen[str] | None = None
    firewall_added = False
    chutney_started = False
    vms_touched = False
    exit_code = 1
    try:
        require_dependencies()
        metadata.commit, metadata.git_dirty = git_state()
        acquire_sudo()
        repair_workspace_permissions()
        cleanup_stale_chutney_tor()
        vms_touched = True
        recreate_vms()
        server_ip = wait_for_vm_ip(SERVER_VM)
        metadata.server_ip = server_ip
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
            [f"TCP-LISTEN:{SERVER_PORT},bind=127.0.0.1,fork,reuseaddr", f"TCP:{server_ip}:{SERVER_PORT}"],
            result_dir / "socat_service.log",
        )
        console = login_to_console(SERVER_VM)
        configure_server(console)
        start_server(console)
        onion = wait_for_onion(console)
        metadata.onion = onion
        wait_for_hidden_service(onion)
        auth_test_rc = run_auth_test(onion, result_dir, args.auth_timeout)
        metadata.auth_test_exit_code = auth_test_rc
        save_server_log(console, result_dir)
        if auth_test_rc == 0:
            metadata.result = "passed"
            exit_code = 0
            ok("BT-01 completed successfully")
        else:
            metadata.result = "failed"
            exit_code = auth_test_rc
            warn(f"BT-01 protocol test exited with code {auth_test_rc}")
    except KeyboardInterrupt:
        metadata.result = "interrupted"
        exit_code = 130
        warn("Interrupted by user")
    except Exception as exc:
        metadata.result = "error"
        warn(str(exc))
        exit_code = 1
    finally:
        if console is not None:
            try:
                if not (result_dir / "server.log").exists():
                    save_server_log(console, result_dir)
                stop_server(console)
            except Exception as exc:
                warn(f"VM cleanup warning: {exc}")
        close_console(console)
        stop_process(service_socat, "hidden-service socat")
        stop_process(control_socat, "control-port socat")
        remove_firewall_rule_if_added(firewall_added)
        if not args.keep_environment:
            if chutney_started:
                subprocess.run(
                    ["bash", str(CHUTNEY_MANAGER), "stop"],
                    cwd=str(PROJECT_ROOT),
                    stdout=subprocess.DEVNULL,
                    stderr=subprocess.DEVNULL,
                )
            if vms_touched:
                destroy_vms()
        else:
            info("Keeping VMs and Chutney running (--keep-environment)")
        (result_dir / "run.json").write_text(json.dumps(asdict(metadata), indent=2) + "\n", encoding="utf-8")
        info(f"Results directory: {result_dir}")
    return exit_code

if __name__ == "__main__":
    sys.exit(main())

