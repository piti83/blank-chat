import sys
import pexpect
import time
from pathlib import Path

sys.path.append(str(Path(__file__).resolve().parent.parent / "scripts"))
from utils import print_info, print_success, print_error

TOR_SERVER_CONTROL_PORT = 8006
GATEWAY_IP = "192.168.122.1"

def login_to_console(vm_name: str) -> pexpect.spawn:
    print_info(f"Connecting to the console of VM {vm_name}...")

    child = pexpect.spawn(f"virsh -c qemu:///system console {vm_name}", encoding='utf-8')
    child.logfile_read = sys.stdout

    try:
        child.expect(r"Escape character is")
        time.sleep(1)

        logged_in = False
        for _ in range(30):
            child.send("\r\n")
            index = child.expect([r"(?i)login:", r"root@.*#", pexpect.TIMEOUT], timeout=2)

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

def setup_server():
    child = login_to_console("bc-server")

    child.send("killall blank_chat_server 2>/dev/null\r\n")
    child.expect(r"root@.*#")

    print_info("Cloning Tor authentication cookie into the VM...")

    project_root = Path(__file__).resolve().parent.parent
    cookie_path = project_root / ".chutney" / "net" / "nodes" / "006r" / "control_auth_cookie"

    if not cookie_path.exists():
        print_error(f"Cookie file not found on host: {cookie_path}")
        sys.exit(1)

    with open(cookie_path, "rb") as f:
        cookie_bytes = f.read()

    cookie_hex = "".join(f"\\x{b:02x}" for b in cookie_bytes)
    vm_cookie_dir = str(cookie_path.parent)
    vm_cookie_path = str(cookie_path)

    child.send(f"mkdir -p {vm_cookie_dir}\r\n")
    child.expect(r"root@.*#")

    child.send(f"printf '{cookie_hex}' > {vm_cookie_path}\r\n")
    child.expect(r"root@.*#")

    print_info("Generating server configuration (server_config.toml)...")

    lines = [
        "mkdir -p /etc/blank-chat",
        "echo '[network]' > /etc/blank-chat/server_config.toml",
        "echo 'listen_host = \"0.0.0.0\"' >> /etc/blank-chat/server_config.toml",
        "echo 'listen_port = 8080' >> /etc/blank-chat/server_config.toml",
        f"echo 'tor_control_host = \"{GATEWAY_IP}\"' >> /etc/blank-chat/server_config.toml",
        f"echo 'tor_control_port = {TOR_SERVER_CONTROL_PORT}' >> /etc/blank-chat/server_config.toml",
        "echo '' >> /etc/blank-chat/server_config.toml",
        "echo '[security]' >> /etc/blank-chat/server_config.toml",
        "echo 'memory_quota_percent = 80' >> /etc/blank-chat/server_config.toml",
        "echo 'max_messages_per_mailbox = 50' >> /etc/blank-chat/server_config.toml",
    ]

    for cmd in lines:
        child.send(cmd + "\r\n")
        child.expect(r"root@.*#")

    print_info("Starting Blank Chat server in background (attached to PTY)...")
    child.logfile_read = sys.stdout

    child.send("cd /etc/blank-chat && blank_chat_server &\r\n")

    try:
        child.expect(r'Hidden Service Address:\s*([a-z2-7]{56})', timeout=20)
        onion_address = child.match.group(1) + ".onion"
        print_success(f"\nServer started! Captured address: {onion_address}")

        child.sendcontrol(']')
        return onion_address

    except pexpect.TIMEOUT:
        print_error("\nTimeout waiting for .onion address. Process is stuck or silent.")
        sys.exit(1)

def main():
    print_info("Starting test orchestration...")
    onion_address = setup_server()
    print_info(f"Next step: configuring clients with address {onion_address}")

if __name__ == "__main__":
    main()
