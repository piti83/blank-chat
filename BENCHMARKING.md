# Blank Chat Test Environment Manual Setup Guide

This guide outlines the steps required to properly connect the virtual network (QEMU/KVM) with the local Tor network instance (Chutney) on the host (Arch Linux). The main issue this solves is Tor's default binding to the `127.0.0.1` interface and the network isolation of the virtual machines.

## Step 1: Verify modifications in `chutney_manager.sh`
Ensure that the Chutney management script disables Tor authentication and generates a fake cookie for the Python orchestrator. 
The `start_network()` function must contain the following lines:
* Force no authentication: `echo "CookieAuthentication 0" >> "$conf"`
* Create a 32-byte zeroed cookie file: `dd if=/dev/zero of="$DATA_DIR/nodes/006r/control_auth_cookie" bs=32 count=1`

## Step 2: Unblock traffic on the hypervisor firewall
By default, `libvirt` blocks traffic from virtual machines to services running on the host. You must unconditionally allow incoming traffic on the virtual bridge interface (usually `virbr0`).
Execute on the host:
```bash
sudo iptables -I INPUT -i virbr0 -j ACCEPT
```

## Step 3: Find the server VM IP address
To establish the return tunnel, you need to know the DHCP-assigned IP address of the `bc-server` virtual machine.
**Method A (from the host):**
```bash
virsh -c qemu:///system domifaddr bc-server
```

**Method B (from the VM console):**
```bash
virsh -c qemu:///system console bc-server
# After logging in as root, run:
ip a
```
*Note this address (e.g., `192.168.122.15`). You will need it in the next step.*

## Step 4: Establish `socat` tunnels
You need to open two additional terminal windows (or run the processes in the background). These tunnels bypass routing traps and Tor binding limitations.

**Terminal 1 (Traffic: VM -> Tor ControlPort):**
Catch the connection request from the VM hitting the gateway on port `8006` and forward it to Tor on the local host.
```bash
socat TCP-LISTEN:8006,bind=192.168.122.1,fork,reuseaddr TCP:127.0.0.1:8006
```

**Terminal 2 (Traffic: Tor -> Server in VM):**
Receive encrypted Tor traffic from the hidden service on port `8080` and forward it to the correct Yocto machine (replace `<VM_IP>` with the IP from Step 3).
```bash
socat TCP-LISTEN:8080,bind=127.0.0.1,fork,reuseaddr TCP:<VM_IP>:8080
```

## Step 5: Reset the environment and run the orchestrator
Always ensure the network starts completely fresh before testing so that the configuration modifications from Step 1 are correctly applied.

**Main Terminal:**
```bash
# Reset the Chutney network
chutney-stop
chutney-clean
chutney-start

# Run the orchestrating script
python3 benchmarks/orchestrator.py
```
