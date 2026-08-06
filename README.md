![blank-chat](docs/ascii-art.png)

---


**Blank Chat** is a high-assurance, asynchronous, Zero-Trust text messenger built for hostile operational environments. It leverages a **Blind Relay architecture**, strict **RAM-Only** server constraints, and advanced **traffic obfuscation** to guarantee absolute confidentiality, integrity, and network-level anonymity.

Built from the ground up in modern **C++20**, Blank Chat assumes the network is compromised and hardware can be seized. It leaves no trace, trusts no server, and cryptographically blends into the background noise of the Tor network.

---

## Key Features

### RAM-Only Ephemeral Backend
The central relay server operates in a strict Zero-Knowledge, RAM-only environment. 
* **Zero Disk Footprint:** Messages and queues are dynamically allocated in heap/stack and never serialized to persistent storage.
* **OS-Level Hardening:** Utilizing a custom-built **Yocto Project** Linux distribution, the kernel is stripped of hibernation (ACPI S4) and SWAP capabilities.
* **Locked Memory:** Prevents the OS from paging cryptographic buffers. Once a message is polled, irreversibly obliterates the data, maintaining a "Clean Slate" environment.

### Advanced Cryptography & E2EE
* **Perfect Forward Secrecy (PFS):** Utilizes X25519 ECDHE ephemeral key exchanges to prevent retrospective decryption of historical traffic if long-term keys are compromised.
* **Ed25519 Identity Keys:** Static long-term keys are strictly isolated from session encryption, used solely for cryptographic signatures to prevent MITM attacks.
* **Zero-Knowledge Architecture:** The Blind Relay server only sees encrypted blobs and unlinked `MailboxIDs`. It cannot mathematically correlate sender and receiver.

### Traffic Obfuscation
Standard Tor routing is insufficient against volumetric Website Fingerprinting. Blank Chat implements L7 obfuscation:
* **Adaptive Volumetric Padding:** Every application-layer frame is algorithmically padded to precisely fit the 498-byte payload space of a Tor `RELAY` cell, eliminating size signatures.
* **Temporal Obfuscation:** Configurable transmission modes including **Constant Bit Rate (CBR)** and **Poisson process**. The client emits traffic at rigid intervals (e.g., every 5 seconds). If no data is available, indistinguishable `POLL` frames are sent, paralyzing temporal correlation vectors.

### Deterministic OOB Identity Exchange
To eliminate the need for centralized PKI, Identity Keys are exchanged via an Out-Of-Band channel using a deterministic **BIP39 dictionary encoding**. 256 bits of key entropy map perfectly to a 24-word mnemonic, allowing secure physical or voice exchange without the risk of homophonic errors.

### Anti-Forensic TUI
* **No GUI Bloat:** Operates entirely within the terminal via a custom-built, secure REPL.
* **Memory Wiping:** Periodically emits CSI escape sequences to forcefully purge terminal emulator history buffers.
* **Kernel Protections:** Implements syscall filtering and disables core dumps.


## Architecture Overview

The system consists of three completely isolated layers:

1. **The Client (TUI):** Manages local encrypted address books, performs E2EE/PFS encryption, executes PoW (Hashcash) for anti-spam, and drives the async networking engine.
2. **The Tor SOCKS5h Layer:** All traffic is forcefully routed through Tor Hidden Services (`.onion`). DNS resolution is delegated to the Tor circuit (`SOCKS5h`), preventing local DNS leaks.
3. **The Blind Relay Server:** A high-performance, async C++ daemon running on a custom Yocto Linux image. It verifies PoW, queues fixed-size encrypted frames, and instantly wipes memory upon delivery.

## Getting Started

### Prerequisites
* Docker (for the Yocto build environment)
* Python 3.10+
* GitHub CLI (optional, for fetching pre-built SDKs)

### 1. Environment Setup
The project uses a Python-based wrapper to manage CMake and Yocto environments seamlessly.
```bash
git clone <https/ssh>
cd blank-chat
# Source the helper aliases
source bc-env.sh
```

### 2. Building the Project
You can build the binaries directly or generate the full Yocto OS image.

**Standard SDK Build:**
```bash
build <preset>
```

**Build Yocto OS Images (Server & Client):**
```bash
build-yocto
```

### 4. Running the Client/Server
To run client or server it is recommended to build whole Yocto image.
1. Build Yocto `.wic` Images
2. Create VM or install the images on some form of drive
3. Boot into the image
4. Run server
5. Run clients
6. Follow the interactive REPL to generate your Identity Key and manage your address book. Type `help` for a list of commands.

---

## Testing & Code Quality

Blank Chat maintains strict aerospace-grade software quality standards.
* **Unit Tests:** `bctest yocto-debug`
* **Memory/Leak Checks:** `valgrind-test`
* **Static Analysis:** Integrated `clang-tidy` checks.
* **Code Coverage:** `coverage` (Outputs to `build/yocto-coverage/coverage_report/index.html`)

---

## Contributing

I welcome every contributor that wants to help develop this project.

1. Check out our [`CONTRIBUTING.md`](CONTRIBUTING.md) for coding standards and other related information.
2. Pick an issue tagged `good first issue` or `help wanted`.
3. Create a dedicated branch for that issue if it doesn't exist yet.
3. Submit a Pull Request targeting the `main` branch.
4. Ensure all CI checks (ASAN, UBSAN, Test Coverage) pass.

## License

This project is licensed under the **GNU General Public License v3.0** (GPLv3) - see the [LICENSE](LICENSE) file for details.
