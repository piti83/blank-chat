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
STRESS_DIR = BENCHMARKS_DIR / "stress"

VM_MANAGER = BENCHMARKS_DIR / "vm_manager.py"
CHUTNEY_MANAGER = BENCHMARKS_DIR / "chutney_manager.sh"
LOADGEN = STRESS_DIR / "loadgen.py"
MONITOR_SCRIPT = STRESS_DIR / "monitor.sh"

SERVER_VM = "bc-server"
CLIENT_VMS = ("bc-client-1", "bc-client-2")
ALL_VMS = (SERVER_VM, *CLIENT_VMS)

GATEWAY_IP = "192.168.122.1"
TOR_CONTROL_PORT = 8006
TOR_SOCKS_HOST = "127.0.0.1"
TOR_SOCKS_PORT = 9050
SERVER_PORT = 8080

REMOTE_SERVER_CONFIG = "/etc/blank-chat/server_config.toml"
REMOTE_SERVER_LOG = "/tmp/stress_server.log"
REMOTE_SERVER_PID = "/tmp/stress_server.pid"
REMOTE_MONITOR = "/tmp/stress_monitor.sh"
REMOTE_MONITOR_PID = "/tmp/stress_monitor.pid"
REMOTE_METRICS = "/tmp/stress_metrics.csv"
REMOTE_MONITOR_LOG = "/tmp/stress_monitor.log"

PROMPT = "__BC_STRESS_PROMPT__ "


class StressError(RuntimeError):
    pass


@dataclass
class RunMetadata:
    started_at: str
    server_vm: str = SERVER_VM
    server_ip: str = ""
    onion: str = ""
    clients: int = 100
    payload_size: int = 1024 * 1024
    max_sent_mib: int = 1800
    setup_only: bool = False
    loadgen_exit_code: int | None = None
    result: str = "incomplete"


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
    cwd: Path | None = None,
) -> subprocess.CompletedProcess[str]:
    info("$ " + shlex.join(cmd))
    result = subprocess.run(
        cmd,
        cwd=str(cwd or PROJECT_ROOT),
        text=True,
        capture_output=capture,
    )
    if check and result.returncode != 0:
        detail = ""
        if capture:
            detail = (result.stderr or result.stdout or "").strip()
        raise StressError(
            f"Command failed with exit code {result.returncode}: {shlex.join(cmd)}"
            + (f"\n{detail}" if detail else "")
        )
    return result


def require_dependencies() -> None:
    required = ("virsh", "virt-install", "qemu-img", "socat", "sudo", "iptables", "pkill")
    missing = [name for name in required if subprocess.run(
        ["sh", "-c", f"command -v {shlex.quote(name)} >/dev/null 2>&1"]
    ).returncode != 0]

    if missing:
        raise StressError("Missing host dependencies: " + ", ".join(missing))

    for path in (VM_MANAGER, CHUTNEY_MANAGER, LOADGEN, MONITOR_SCRIPT):
        if not path.exists():
            raise StressError(f"Required file does not exist: {path}")


def acquire_sudo() -> None:
    info("Acquiring sudo credentials for firewall setup...")
    run(["sudo", "-v"])
    ok("sudo credentials ready")


def repair_workspace_permissions() -> None:
    """Repair root-owned benchmark runtime files left by an earlier sudo run."""
    import os
    import pwd
    import grp

    uid = os.getuid()
    gid = os.getgid()
    user = pwd.getpwuid(uid).pw_name
    group = grp.getgrgid(gid).gr_name

    paths = [
        PROJECT_ROOT / ".chutney",
        BENCHMARKS_DIR / "results",
    ]

    for path in paths:
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


def _project_chutney_tor_pids() -> list[int]:
    runtime = str((PROJECT_ROOT / ".chutney" / "net").resolve())
    result = subprocess.run(
        ["ps", "-eo", "pid=,comm=,args="],
        text=True,
        capture_output=True,
        check=True,
    )

    pids: list[int] = []
    for raw_line in result.stdout.splitlines():
        line = raw_line.strip()
        if not line:
            continue

        parts = line.split(None, 2)
        if len(parts) < 3:
            continue

        pid_text, comm, args = parts
        if comm != "tor":
            continue

        if runtime not in args:
            continue

        try:
            pids.append(int(pid_text))
        except ValueError:
            continue

    return pids


def cleanup_stale_chutney_tor() -> None:
    """Kill only Tor processes belonging to this repository's Chutney runtime."""
    pids = _project_chutney_tor_pids()
    if not pids:
        info("No stale project Chutney Tor processes found")
        return

    warn(
        "Found stale Chutney Tor processes from this repository: "
        + ", ".join(str(pid) for pid in pids)
    )

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
        warn(
            "Some stale Chutney Tor processes ignored SIGTERM; sending SIGKILL: "
            + ", ".join(str(pid) for pid in remaining)
        )
        run(["sudo", "kill", "-KILL", *[str(pid) for pid in remaining]], check=False)
        time.sleep(0.5)

    remaining = _project_chutney_tor_pids()
    if remaining:
        raise StressError(
            "Could not stop stale project Chutney Tor processes: "
            + ", ".join(str(pid) for pid in remaining)
        )

    ok("Stale Chutney Tor processes killed")


def recreate_vms() -> None:
    info("Recreating benchmark VMs (2 GiB RAM / 2 vCPU each)...")
    run([sys.executable, str(VM_MANAGER)])
    ok("VMs started")


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


def wait_for_vm_ip(vm_name: str, timeout: float = 120.0) -> str:
    info(f"Waiting for DHCP address of {vm_name}...")
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        ip = get_vm_ip_once(vm_name)
        if ip:
            ok(f"{vm_name} IP: {ip}")
            return ip
        time.sleep(2)
    raise StressError(f"Timed out waiting for IP address of {vm_name}")


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
            raise StressError(f"Unexpected SOCKS5 greeting response: {response!r}")
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
        subprocess.run(
            ["pkill", "-f", pattern],
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
    time.sleep(0.3)


def start_socat(args: list[str], log_path: Path) -> subprocess.Popen[str]:
    log_path.parent.mkdir(parents=True, exist_ok=True)
    log_file = log_path.open("w", encoding="utf-8")
    proc = subprocess.Popen(
        ["socat", *args],
        stdout=log_file,
        stderr=subprocess.STDOUT,
        text=True,
    )
    log_file.close()

    time.sleep(0.5)
    if proc.poll() is not None:
        detail = log_path.read_text(encoding="utf-8", errors="replace")
        raise StressError(f"socat failed to start:\n{detail}")

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
    child = pexpect.spawn(
        "virsh",
        ["-c", "qemu:///system", "console", vm_name],
        encoding="utf-8",
        timeout=10,
    )

    try:
        child.expect(r"Escape character is", timeout=20)
    except pexpect.TIMEOUT as exc:
        child.close(force=True)
        raise StressError(f"Could not attach to {vm_name} serial console") from exc

    deadline = time.monotonic() + timeout
    logged_in = False

    while time.monotonic() < deadline:
        child.send("\r\n")
        idx = child.expect(
            [
                r"(?i)login:",
                r"root@[^#\r\n]*#",
                r"# ",
                pexpect.TIMEOUT,
                pexpect.EOF,
            ],
            timeout=3,
        )

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
        raise StressError(f"Timed out logging in to {vm_name}")

    child.send("stty -echo\r\n")
    child.expect(r"# ", timeout=10)

    child.send(f"export PS1='{PROMPT}'\r\n")
    child.expect(re.escape(PROMPT), timeout=10)

    ok(f"Logged into {vm_name} as root")
    return child


def console_cmd(
    child: pexpect.spawn,
    command: str,
    *,
    timeout: float = 30.0,
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
    end_pattern = re.escape(end_marker) + r":(\d+)"
    child.expect(end_pattern, timeout=timeout)

    output = child.before.replace("\r", "").strip()
    rc = int(child.match.group(1))

    child.expect(re.escape(PROMPT), timeout=10)

    if check and rc != 0:
        raise StressError(
            f"Command failed inside VM (rc={rc}): {command}\n{output}"
        )

    return output, rc

def upload_text(child: pexpect.spawn, remote_path: str, text: str) -> None:
    qpath = shlex.quote(remote_path)
    console_cmd(child, f": > {qpath}")

    for line in text.splitlines():
        console_cmd(
            child,
            f"printf '%s\\n' {shlex.quote(line)} >> {qpath}",
        )


def configure_server(child: pexpect.spawn) -> None:
    info("Writing stress-test server configuration...")
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
    console_cmd(
        child,
        f"rm -f {REMOTE_SERVER_LOG} {REMOTE_SERVER_PID} "
        f"{REMOTE_METRICS} {REMOTE_MONITOR_PID} {REMOTE_MONITOR_LOG}",
    )
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
            f"grep -Eo '[a-z2-7]{{56}}(\\.onion)?' {REMOTE_SERVER_LOG} "
            "2>/dev/null | tail -n 1 || true",
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
            log_text, _ = console_cmd(
                child,
                f"cat {REMOTE_SERVER_LOG} 2>/dev/null || true",
                check=False,
            )
            raise StressError(f"blank_chat_server exited before publishing onion address:\n{log_text}")

        time.sleep(1)

    log_text, _ = console_cmd(
        child,
        f"cat {REMOTE_SERVER_LOG} 2>/dev/null || true",
        check=False,
    )
    raise StressError(f"Timed out waiting for onion address.\nServer log:\n{log_text}")



def wait_for_hidden_service(onion: str, result_dir: Path, timeout: float = 60.0) -> None:
    info("Waiting until the hidden service is reachable end-to-end...")
    deadline = time.monotonic() + timeout
    attempt = 0

    while time.monotonic() < deadline:
        attempt += 1
        probe_summary = result_dir / ".onion_probe.json"
        cmd = [
            sys.executable,
            str(LOADGEN),
            "--onion",
            onion,
            "--clients",
            "1",
            "--setup-only",
            "--timeout",
            "5",
            "--probe-timeout",
            "5",
            "--summary-json",
            str(probe_summary),
        ]

        result = subprocess.run(
            cmd,
            cwd=str(PROJECT_ROOT),
            stdout=subprocess.DEVNULL,
            stderr=subprocess.DEVNULL,
        )
        if result.returncode == 0:
            probe_summary.unlink(missing_ok=True)
            ok(f"Hidden service is reachable (attempt {attempt})")
            return

        time.sleep(2)

    raise StressError(f"Hidden service {onion} did not become reachable within {timeout:.0f}s")

def start_monitor(child: pexpect.spawn) -> None:
    info("Uploading and starting in-VM memory monitor...")
    monitor_text = MONITOR_SCRIPT.read_text(encoding="utf-8")
    upload_text(child, REMOTE_MONITOR, monitor_text)
    console_cmd(child, f"chmod +x {REMOTE_MONITOR}")
    console_cmd(
        child,
        f"{REMOTE_MONITOR} {REMOTE_METRICS} $(cat {REMOTE_SERVER_PID}) "
        f"> {REMOTE_MONITOR_LOG} 2>&1 & echo $! > {REMOTE_MONITOR_PID}",
    )
    time.sleep(2)

    output, rc = console_cmd(
        child,
        f"wc -l < {REMOTE_METRICS} 2>/dev/null",
        check=False,
    )
    if rc != 0:
        monitor_log, _ = console_cmd(
            child,
            f"cat {REMOTE_MONITOR_LOG} 2>/dev/null || true",
            check=False,
        )
        raise StressError(f"Monitor did not start correctly:\n{monitor_log}")

    try:
        lines = int(output.splitlines()[-1].strip())
    except (ValueError, IndexError) as exc:
        raise StressError(f"Unexpected monitor line count: {output!r}") from exc

    if lines < 2:
        raise StressError("Monitor started but has not produced a sample")

    ok("Memory monitor is sampling VmRSS/VmLck every second")


def stop_monitor(child: pexpect.spawn) -> None:
    console_cmd(
        child,
        f"kill $(cat {REMOTE_MONITOR_PID} 2>/dev/null) 2>/dev/null || true",
        check=False,
    )
    time.sleep(0.3)


def remote_cat(child: pexpect.spawn, path: str) -> str:
    output, _ = console_cmd(
        child,
        f"cat {shlex.quote(path)} 2>/dev/null || true",
        timeout=60,
        check=False,
    )
    return output


def run_loadgen(args: argparse.Namespace, onion: str, result_dir: Path) -> int:
    summary_path = result_dir / "loadgen_summary.json"

    cmd = [
        sys.executable,
        str(LOADGEN),
        "--onion",
        onion,
        "--clients",
        str(args.clients),
        "--payload-size",
        str(args.payload_size),
        "--max-sent-mib",
        str(args.max_sent_mib),
        "--summary-json",
        str(summary_path),
    ]

    if args.setup_only:
        cmd.append("--setup-only")

    info("Starting load generator...")
    info("$ " + shlex.join(cmd))
    result = subprocess.run(cmd, cwd=str(PROJECT_ROOT))
    return result.returncode


def save_vm_artifacts(child: pexpect.spawn, result_dir: Path) -> None:
    info("Collecting stress-test artifacts from server VM...")
    stop_monitor(child)

    metrics = remote_cat(child, REMOTE_METRICS)
    server_log = remote_cat(child, REMOTE_SERVER_LOG)
    monitor_log = remote_cat(child, REMOTE_MONITOR_LOG)

    (result_dir / "metrics.csv").write_text(
        metrics + ("\n" if metrics and not metrics.endswith("\n") else ""),
        encoding="utf-8",
    )
    (result_dir / "server.log").write_text(
        server_log + ("\n" if server_log and not server_log.endswith("\n") else ""),
        encoding="utf-8",
    )
    (result_dir / "monitor.log").write_text(
        monitor_log + ("\n" if monitor_log and not monitor_log.endswith("\n") else ""),
        encoding="utf-8",
    )
    ok(f"Artifacts saved to {result_dir}")


def stop_server(child: pexpect.spawn) -> None:
    console_cmd(
        child,
        f"kill $(cat {REMOTE_SERVER_PID} 2>/dev/null) 2>/dev/null || true",
        check=False,
    )


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
        description="End-to-end Blank Chat stress-test orchestration"
    )
    parser.add_argument("--clients", type=int, default=100)
    parser.add_argument("--payload-size", type=int, default=1024 * 1024)
    parser.add_argument("--max-sent-mib", type=int, default=1800)
    parser.add_argument(
        "--setup-only",
        action="store_true",
        help="build the entire environment and run loadgen connectivity/auth smoke test only",
    )
    parser.add_argument(
        "--keep-environment",
        action="store_true",
        help="leave VMs and Chutney running after the run for debugging",
    )
    args = parser.parse_args()

    if args.clients <= 0:
        parser.error("--clients must be > 0")
    if not 1 <= args.payload_size <= 1024 * 1024:
        parser.error("--payload-size must be between 1 and 1048576")
    if args.max_sent_mib <= 0:
        parser.error("--max-sent-mib must be > 0")

    return args


def main() -> int:
    args = parse_args()

    stamp = datetime.now().strftime("%Y%m%d_%H%M%S")
    result_dir = BENCHMARKS_DIR / "results" / "stress" / stamp
    result_dir.mkdir(parents=True, exist_ok=True)

    metadata = RunMetadata(
        started_at=datetime.now().astimezone().isoformat(),
        clients=args.clients,
        payload_size=args.payload_size,
        max_sent_mib=args.max_sent_mib,
        setup_only=args.setup_only,
    )

    console: pexpect.spawn | None = None
    control_socat: subprocess.Popen[str] | None = None
    service_socat: subprocess.Popen[str] | None = None
    firewall_added = False
    chutney_started = False

    exit_code = 1

    try:
        require_dependencies()
        acquire_sudo()
        repair_workspace_permissions()
        cleanup_stale_chutney_tor()

        recreate_vms()
        server_ip = wait_for_vm_ip(SERVER_VM)
        metadata.server_ip = server_ip

        start_chutney()
        chutney_started = True
        check_socks5()

        firewall_added = ensure_firewall_rule()
        stop_stale_socat()

        control_socat = start_socat(
            [
                f"TCP-LISTEN:{TOR_CONTROL_PORT},bind={GATEWAY_IP},fork,reuseaddr",
                f"TCP:127.0.0.1:{TOR_CONTROL_PORT}",
            ],
            result_dir / "socat_control.log",
        )

        service_socat = start_socat(
            [
                f"TCP-LISTEN:{SERVER_PORT},bind=127.0.0.1,fork,reuseaddr",
                f"TCP:{server_ip}:{SERVER_PORT}",
            ],
            result_dir / "socat_service.log",
        )

        console = login_to_console(SERVER_VM)
        configure_server(console)
        start_server(console)

        onion = wait_for_onion(console)
        metadata.onion = onion

        wait_for_hidden_service(onion, result_dir)
        start_monitor(console)

        loadgen_rc = run_loadgen(args, onion, result_dir)
        metadata.loadgen_exit_code = loadgen_rc

        save_vm_artifacts(console, result_dir)

        if loadgen_rc == 0:
            metadata.result = "passed"
            exit_code = 0
            ok("Stress test completed successfully")
        else:
            metadata.result = "failed"
            exit_code = loadgen_rc
            warn(f"Load generator exited with code {loadgen_rc}")

    except KeyboardInterrupt:
        metadata.result = "interrupted"
        exit_code = 130
        warn("Interrupted by user")
    except Exception as exc:
        metadata.result = "error"
        metadata.loadgen_exit_code = metadata.loadgen_exit_code
        warn(str(exc))
        exit_code = 1
    finally:
        if console is not None:
            try:
                metrics_path = result_dir / "metrics.csv"
                if not metrics_path.exists():
                    try:
                        save_vm_artifacts(console, result_dir)
                    except Exception as exc:
                        warn(f"Could not collect VM artifacts during cleanup: {exc}")

                stop_monitor(console)
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
            destroy_vms()
        else:
            info("Keeping VMs and Chutney running (--keep-environment)")

        (result_dir / "run.json").write_text(
            json.dumps(asdict(metadata), indent=2) + "\n",
            encoding="utf-8",
        )

        info(f"Results directory: {result_dir}")

    return exit_code


if __name__ == "__main__":
    sys.exit(main())
