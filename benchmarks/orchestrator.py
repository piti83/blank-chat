import sys
import pexpect
import time
import subprocess
import re
import atexit
import argparse
import base64
import socket
from pathlib import Path
import os

sys.path.append(str(Path(__file__).resolve().parent.parent / "scripts"))
from utils import print_info, print_success, print_error

TOR_SERVER_CONTROL_PORT = 8006
TOR_SOCKS_PORT = 9050
GATEWAY_IP = "192.168.122.1"

def check_tor_health():
    print_info(f"Performing health check on Tor SOCKS proxy (127.0.0.1:{TOR_SOCKS_PORT})...")
    try:
        s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        s.settimeout(3)
        s.connect(("127.0.0.1", TOR_SOCKS_PORT))
        s.sendall(b"\x05\x01\x00")
        resp = s.recv(2)
        if resp != b"\x05\x00":
            print_error(f"Tor SOCKS proxy returned invalid greeting: {resp}")
            sys.exit(1)
        s.close()
        print_success("Tor SOCKS proxy is alive and accepting NoAuth!")
    except Exception as e:
        print_error(f"Tor SOCKS proxy health check failed: {e}")
        print_error("The Chutney network is dead or not listening! Please run:")
        print_error("chutney-stop && chutney-clean && chutney-start")
        sys.exit(1)

def setup_tunnels(server_vm_name: str = "bc-server"):
    subprocess.run(["killall", "socat"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)

    check_tor_health()

    print_info(f"Retrieving IP address for {server_vm_name}...")

    vm_ip = None
    for _ in range(10):
        res = subprocess.run(["virsh", "-c", "qemu:///system", "domifaddr", server_vm_name], capture_output=True, text=True)
        match = re.search(r"(\d+\.\d+\.\d+\.\d+)/", res.stdout)
        if match:
            vm_ip = match.group(1)
            break
        time.sleep(2)

    if not vm_ip:
        print_error("Could not determine Server VM IP address. Ensure it is running.")
        sys.exit(1)

    print_success(f"Found Server IP: {vm_ip}. Starting socat tunnels...")

    p1 = subprocess.Popen(["socat", f"TCP-LISTEN:{TOR_SERVER_CONTROL_PORT},bind={GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{TOR_SERVER_CONTROL_PORT}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p2 = subprocess.Popen(["socat", "TCP-LISTEN:8080,bind=127.0.0.1,fork,reuseaddr", f"TCP:{vm_ip}:8080"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    p3 = subprocess.Popen(["socat", f"TCP-LISTEN:{TOR_SOCKS_PORT},bind={GATEWAY_IP},fork,reuseaddr", f"TCP:127.0.0.1:{TOR_SOCKS_PORT}"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def cleanup():
        print_info("Cleaning up socat tunnels...")
        p1.terminate()
        p2.terminate()
        p3.terminate()

    atexit.register(cleanup)

def start_tshark(profile: str, mode: str) -> subprocess.Popen:
    pcap_filename = f"capture_{profile}_{mode}_{int(time.time())}.pcap"
    tmp_pcap_path = f"/tmp/{pcap_filename}"
    final_pcap_path = os.path.abspath(pcap_filename)

    print_info(f"Starting tshark capture on virbr0 (temporarily writing to {tmp_pcap_path})...")

    tshark_cmd = ["sudo", "tshark", "-i", "virbr0", "-f", "tcp port 8080 or tcp port 9050", "-w", tmp_pcap_path]

    proc = subprocess.Popen(tshark_cmd, stdout=subprocess.DEVNULL)

    def cleanup_tshark():
        subprocess.run(["sudo", "killall", "tshark"], stderr=subprocess.DEVNULL, stdout=subprocess.DEVNULL)
        time.sleep(1)

        if os.path.exists(tmp_pcap_path):
            subprocess.run(["sudo", "mv", tmp_pcap_path, final_pcap_path])
            subprocess.run(["sudo", "chown", f"{os.getuid()}:{os.getgid()}", final_pcap_path])

    atexit.register(cleanup_tshark)
    return proc

def login_to_console(vm_name: str) -> pexpect.spawn:
    print_info(f"Connecting to the console of VM {vm_name}...")
    child = pexpect.spawn(f"virsh -c qemu:///system console {vm_name}", encoding='utf-8')
    child.logfile_read = sys.stdout

    try:
        child.expect(r"Escape character is", timeout=15)
        time.sleep(1)
        child.sendcontrol('c')
        time.sleep(0.5)

        logged_in = False
        for _ in range(30):
            child.send("\r\n")
            index = child.expect([r"(?i)login:", r"root@.*#", pexpect.TIMEOUT], timeout=3)

            if index == 0:
                child.send("root\r\n")
                idx_inner = child.expect([r"root@.*#", pexpect.TIMEOUT], timeout=5)
                if idx_inner == 0:
                    print_success(f"\nLogged into {vm_name} as root.")
                    logged_in = True
                    break
            elif index == 1:
                print_success(f"\nAuto-logged into {vm_name} as root.")
                logged_in = True
                break

        if not logged_in:
            print_error(f"\nTimeout while waiting to login to {vm_name}.")
            sys.exit(1)

    except Exception as e:
        print_error(f"\nError while logging into console: {e}")
        sys.exit(1)

    child.send("stty -echo\r\n")
    child.expect(r"root@.*#")
    child.logfile_read = None
    return child

def setup_server() -> str:
    child = login_to_console("bc-server")
    child.send("killall blank_chat_server 2>/dev/null\r\n")
    child.expect(r"root@.*#")

    lines = [
        "mkdir -p /etc/blank-chat",
        "echo '[network]' > /etc/blank-chat/server_config.toml",
        "echo 'listen_host = \"0.0.0.0\"' >> /etc/blank-chat/server_config.toml",
        "echo 'listen_port = 8080' >> /etc/blank-chat/server_config.toml",
        f"echo 'tor_control_host = \"{GATEWAY_IP}\"' >> /etc/blank-chat/server_config.toml",
        f"echo 'tor_control_port = {TOR_SERVER_CONTROL_PORT}' >> /etc/blank-chat/server_config.toml",
        "echo '[security]' >> /etc/blank-chat/server_config.toml",
        "echo 'memory_quota_percent = 80' >> /etc/blank-chat/server_config.toml",
        "echo 'max_messages_per_mailbox = 50' >> /etc/blank-chat/server_config.toml"
    ]

    for cmd in lines:
        child.send(cmd + "\r\n")
        time.sleep(0.05)
        child.expect(r"root@.*#")

    print_info("Starting Blank Chat server in background...")
    child.send("cd /etc/blank-chat && blank_chat_server &\r\n")

    try:
        child.expect(r'Hidden Service Address:\s*([a-z2-7]{56})', timeout=20)
        onion_address = child.match.group(1) + ".onion"
        print_success(f"\nServer started! Captured address: {onion_address}")
        child.sendcontrol(']')
        return onion_address
    except pexpect.TIMEOUT:
        print_error("\nTimeout waiting for .onion address.")
        sys.exit(1)

def setup_client(vm_name: str, onion_address: str, mode: str):
    child = login_to_console(vm_name)
    child.send("killall blank_chat_client 2>/dev/null\r\n")
    child.expect(r"root@.*#")

    lines = [
        "mkdir -p /etc/blank-chat",
        "echo '[network]' > /etc/blank-chat/client_config.toml",
        f"echo 'tor_socks_host = \"{GATEWAY_IP}\"' >> /etc/blank-chat/client_config.toml",
        f"echo 'tor_socks_port = {TOR_SOCKS_PORT}' >> /etc/blank-chat/client_config.toml",
        "echo '[relay]' >> /etc/blank-chat/client_config.toml",
        f"echo 'onion_address = \"{onion_address}\"' >> /etc/blank-chat/client_config.toml",
        "echo 'onion_port = 80' >> /etc/blank-chat/client_config.toml",
        "echo '[obfuscation]' >> /etc/blank-chat/client_config.toml",
        f"echo 'mode = \"{mode}\"' >> /etc/blank-chat/client_config.toml",
        "echo 'cbr_interval_ms = 10000' >> /etc/blank-chat/client_config.toml",
        "echo 'poisson_lambda = 0.1' >> /etc/blank-chat/client_config.toml",
        "echo '[storage]' >> /etc/blank-chat/client_config.toml",
        "echo 'contacts_file_path = \"contacts.json\"' >> /etc/blank-chat/client_config.toml",
        "echo '[security]' >> /etc/blank-chat/client_config.toml",
        "echo 'pfs_message_interval = 50' >> /etc/blank-chat/client_config.toml"
    ]

    for cmd in lines:
        child.send(cmd + "\r\n")
        time.sleep(0.05)
        child.expect(r"root@.*#")

    print_info(f"Starting REPL on {vm_name}...")
    child.send("cd /etc/blank-chat && blank_chat_client\r\n")

    idx = child.expect([r"Do you want to generate a new Identity Key now\? \(y/n\):", r"> "], timeout=15)
    if idx == 0:
        child.send("y\n")
        child.expect(r"> ", timeout=10)

    child.send("mykey\n")
    child.expect(r"> ", timeout=5)

    match = re.search(r"([a-z]+(?:-[a-z]+){23})", child.before)
    if not match:
        print_error(f"Failed to capture BIP39 mnemonic from {vm_name}!")
        sys.exit(1)

    return child, match.group(1)

def exchange_keys(c1_child, c1_mnem, c2_child, c2_mnem):
    print_info("Exchanging Identity Keys out-of-band...")
    c1_child.send(f"add client2 {c2_mnem}\n")
    c1_child.expect(r"> ", timeout=5)
    c2_child.send(f"add client1 {c1_mnem}\n")
    c2_child.expect(r"> ", timeout=5)
    print_success("Keys exchanged successfully.")

def extract_logs(child: pexpect.spawn, vm_name: str, target_alias: str, profile: str, mode: str):
    print_info(f"Extracting latency logs from {vm_name}...")
    child.send("exit\n")
    child.expect(r"root@.*#", timeout=5)

    file_path = f"/etc/blank-chat/msg_history/history_{target_alias}.jsonl"

    cmd = f"echo '___LOG_START___'; cat {file_path} 2>/dev/null; echo '___LOG_END___'\r\n"
    child.send(cmd)
    child.expect(r"___LOG_END___", timeout=15)

    raw_output = child.before
    start_idx = raw_output.rfind("___LOG_START___")
    if start_idx == -1:
        print_error(f"Could not find delimiters in {vm_name} output.")
        return

    log_content = raw_output[start_idx + len("___LOG_START___"):]

    ansi_escape = re.compile(r'\x1B(?:[@-Z\\-_]|\[[0-?]*[ -/]*[@-~])')
    log_content = ansi_escape.sub('', log_content)

    log_content = re.sub(r'[\r\n]', '', log_content)

    log_content = log_content.replace('}{', '}\n{')

    json_lines = []
    for line in log_content.split('\n'):
        line = line.strip()
        if line.startswith('{') and line.endswith('}'):
            json_lines.append(line)

    if not json_lines:
        print_error(f"Log file is empty, not found, or corrupted on {vm_name}.")
        return

    local_file = f"latency_log_{profile}_{mode}_{vm_name}.jsonl"
    try:
        with open(local_file, "w") as f:
            for jl in json_lines:
                f.write(jl + "\n")
        print_success(f"Saved {len(json_lines)} log entries from {vm_name} to {local_file}")
    except Exception as e:
        print_error(f"Failed to write logs from {vm_name}: {e}")

    child.expect(r"root@.*#", timeout=5)

def run_simulation(c1_child, c2_child, profile: str, duration_min: int):
    duration_sec = duration_min * 60
    end_time = time.time() + duration_sec

    if profile == "idle":
        print_info(f"Running IDLE profile for {duration_min} minutes. No messages will be sent.")
        while time.time() < end_time:
            time.sleep(1)

    elif profile == "chat":
        print_info(f"Running CHAT profile for {duration_min} minutes (150 bytes every 15s)...")
        counter = 1
        while time.time() < end_time:
            msg_c1 = f"MSG_{counter}_FROM_CLIENT_1_".ljust(150, "A")
            msg_c2 = f"MSG_{counter}_FROM_CLIENT_2_".ljust(150, "A")

            c1_child.send(f"send client2 {msg_c1}\n")
            c1_child.expect(r"> ", timeout=5)

            c2_child.send(f"send client1 {msg_c2}\n")
            c2_child.expect(r"> ", timeout=5)

            counter += 1
            time.sleep(15)

    elif profile == "intensive":
        print_info(f"Running INTENSIVE profile for {duration_min} minutes (continuous load)...")
        counter = 1
        while time.time() < end_time:
            msg_max = f"MSG_{counter}_".ljust(200, "B")
            c1_child.send(f"send client2 {msg_max}\n")
            c1_child.expect(r"> ", timeout=5)
            counter += 1
            time.sleep(0.1)

def main():
    parser = argparse.ArgumentParser(description="Blank Chat Test Orchestrator")
    parser.add_argument("--profile", choices=["idle", "chat", "intensive"], required=True, help="Test profile to run")
    parser.add_argument("--mode", choices=["cbr", "poisson"], default="cbr", help="Obfuscation mode")
    parser.add_argument("--duration", type=int, default=60, help="Test duration in minutes")
    args = parser.parse_args()

    print_info(f"Starting test orchestration (Profile: {args.profile.upper()}, Mode: {args.mode.upper()}, Duration: {args.duration} min)")

    setup_tunnels("bc-server")
    tshark_proc = start_tshark(args.profile, args.mode)

    onion_address = setup_server()
    c1_child, c1_mnem = setup_client("bc-client-1", onion_address, args.mode)
    c2_child, c2_mnem = setup_client("bc-client-2", onion_address, args.mode)

    exchange_keys(c1_child, c1_mnem, c2_child, c2_mnem)

    print_info("Giving Tor network 60 seconds to propagate Hidden Service descriptors...")
    time.sleep(60)

    print_info("Connecting clients to the server (building Tor circuits)...")

    c1_child.send("connect\n")
    c1_child.expect(r"> ", timeout=60)
    print_info(f"bc-client-1 response: {c1_child.before.replace('connect', '').strip()}")

    c2_child.send("connect\n")
    c2_child.expect(r"> ", timeout=60)
    print_info(f"bc-client-2 response: {c2_child.before.replace('connect', '').strip()}")

    print_success("Connection commands executed!")

    run_simulation(c1_child, c2_child, args.profile, args.duration)

    print_info("Simulation loop finished. Waiting 60 seconds for in-flight messages to arrive...")
    time.sleep(60)

    print_success("\nExtracting data...")
    extract_logs(c1_child, "bc-client-1", "client2", args.profile, args.mode)
    extract_logs(c2_child, "bc-client-2", "client1", args.profile, args.mode)

    c1_child.sendcontrol(']')
    c2_child.sendcontrol(']')

    print_success("\n=== ORCHESTRATION COMPLETE ===")
    print_info("Logs and PCAP files have been saved to your host directory.")

if __name__ == "__main__":
    main()
