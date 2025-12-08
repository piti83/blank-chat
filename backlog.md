# BLANK CHAT

# High-Performance Async TCP Chat Server (C++17)
## Product Backlog & Roadmap

This document outlines the development phases for the Reactor-based TCP Server.
**Architecture:** Linux Epoll (Edge Triggered) + Thread Pool.

---

## Phase 0: Infrastructure & Build System
*Foundation setup. Ensuring the code compiles and tests run automatically.*

- [ ] **[BUILD] Setup Modern CMake Configuration**
    - **Description:** Configure the build system using Modern CMake targets.
    - **Technical Details:**
        - Create `CMakeLists.txt` enforcing C++17.
        - Configure strict warning flags (`-Wall -Wextra -Werror -Wpedantic`).
        - Integrate `spdlog` via `FetchContent` or git submodule.
    - **Acceptance Criteria:** `cmake -B build && cmake --build build` compiles a "Hello World" with logging.
    - **Labels:** `BUILD`

- [ ] **[BUILD] Configure GitHub Actions Pipeline**
    - **Description:** automate build and test execution on push.
    - **Technical Details:**
        - Create `.github/workflows/cmake.yml`.
        - Pipeline steps: Checkout -> Install Deps (gtest) -> Configure -> Build -> Test.
    - **Acceptance Criteria:** Green checkmark on GitHub for every commit.
    - **Labels:** `BUILD`

- [ ] **[BUILD] Integrate GoogleTest & Sanitizers**
    - **Description:** Add unit testing framework and memory sanitizers.
    - **Technical Details:**
        - Link `gtest` and `gtest_main`.
        - Add CMake option to enable AddressSanitizer (`-fsanitize=address`) and ThreadSanitizer (`-fsanitize=thread`).
    - **Acceptance Criteria:** `ctest` runs successfully; Memory leaks cause build failure in ASan mode.
    - **Labels:** `BUILD`, `TEST`

---

## Phase 1: Networking Core (The Reactor)
*Building the single-threaded Event Loop.*

- [ ] **[NET] Implement RAII Socket Wrapper**
    - **Description:** Create a safe wrapper for raw C file descriptors to manage lifetime.
    - **Technical Details:**
        - Class `Socket` with deleted copy constructor (move-only).
        - Destructor calls `close()`.
        - `setsockopt` for `SO_REUSEADDR`.
    - **Acceptance Criteria:** Socket creates, binds, and closes without FD leaks (verified by Valgrind).
    - **Labels:** `NET`, `CORE`

- [ ] **[CORE] Implement EpollWrapper Class**
    - **Description:** Encapsulate `epoll_create1`, `epoll_ctl`, `epoll_wait`.
    - **Technical Details:**
        - Methods: `addFd()`, `modifyFd()`, `removeFd()`, `wait()`.
        - Use `std::vector<epoll_event>` for events buffer.
    - **Acceptance Criteria:** Can add a dummy FD and detect events without syscall errors.
    - **Labels:** `CORE`

- [ ] **[NET] Implement Non-Blocking I/O Utilities**
    - **Description:** Utility functions to set FDs to non-blocking mode (required for Edge Triggering).
    - **Technical Details:**
        - `fcntl(fd, F_SETFL, O_NONBLOCK)`.
        - Simple `Buffer` class (`std::vector<uint8_t>`) for raw reading.
    - **Acceptance Criteria:** FDs are correctly flagged as non-blocking.
    - **Labels:** `NET`

- [ ] **[CORE] Implement Main Event Loop (Echo Server)**
    - **Description:** The main reactor loop handling Accept and Read/Write in a single thread.
    - **Technical Details:**
        - Loop on `EpollWrapper::wait()`.
        - If `listenFd` -> `accept()`.
        - If `clientFd` -> `read()` and immediately `write()` (Echo).
    - **Acceptance Criteria:** Telnet client can connect, type text, and see it echoed back.
    - **Labels:** `CORE`, `NET`

---

## Phase 2: Concurrency & Architecture
*Decoupling I/O from Business Logic.*

- [ ] **[THREAD] Implement Thread-Safe Task Queue**
    - **Description:** A synchronized queue to pass data from Reactor to Workers.
    - **Technical Details:**
        - `std::queue` protected by `std::mutex` and `std::condition_variable`.
        - Template class or specific `Task` struct.
    - **Acceptance Criteria:** Producer/Consumer test passes without race conditions (TSan verified).
    - **Labels:** `THREAD`

- [ ] **[THREAD] Implement Worker Thread Pool**
    - **Description:** A fixed pool of threads consuming tasks from the queue.
    - **Technical Details:**
        - `std::vector<std::thread>`.
        - Threads run a loop: `queue.pop()` -> `process()`.
        - Clean shutdown mechanism (poison pill).
    - **Acceptance Criteria:** Tasks are executed by different Thread IDs.
    - **Labels:** `THREAD`, `CORE`

- [ ] **[CORE] Implement Session Management (TcpSession)**
    - **Description:** Class representing a connected client state.
    - **Technical Details:**
        - Inherits `std::enable_shared_from_this`.
        - Holds `inputBuffer`, `outputBuffer`, and `socket`.
        - Separate I/O logic from protocol logic.
    - **Acceptance Criteria:** Session objects are created on Accept and destroyed on Disconnect.
    - **Labels:** `CORE`

---

## Phase 3: Protocol & Logic
*Implementing the Custom Binary TLV Protocol.*

- [ ] **[PROTOCOL] Define TLV Binary Structures**
    - **Description:** Define header and message structs.
    - **Technical Details:**
        - Header: `[Type: 1B][Length: 2B]`.
        - Use `htons`/`ntohs` for Big-Endian network order.
    - **Acceptance Criteria:** Unit tests serialize/deserialize structs correctly.
    - **Labels:** `PROTOCOL`

- [ ] **[PROTOCOL] Implement Stream Fragmentation Handling**
    - **Description:** Handle partial reads and coalesced packets (TCP Streaming nature).
    - **Technical Details:**
        - Accumulate bytes in `Session` buffer.
        - Loop: Check if `size >= Header`. If yes, check if `size >= Header+Len`.
        - Extract full frame.
    - **Acceptance Criteria:** Parser correctly extracts messages even if delivered byte-by-byte.
    - **Labels:** `PROTOCOL`, `NET`

- [ ] **[CORE] Implement Command Dispatcher**
    - **Description:** Router that maps MessageType to handler functions.
    - **Technical Details:**
        - `std::map<uint8_t, HandlerFunc>`.
        - Handlers: `handleLogin`, `handleMsg`, etc.
    - **Acceptance Criteria:** Incoming binary packet triggers specific C++ function.
    - **Labels:** `CORE`, `PROTOCOL`

---

## Phase 4: Features & Reliability
*Business Logic and Robustness.*

- [ ] **[CORE] Implement Thread-Safe User Registry**
    - **Description:** Global map of active users.
    - **Technical Details:**
        - `std::unordered_map<string, shared_ptr<Session>>`.
        - Protected by `std::shared_mutex` (Read-Write Lock).
    - **Acceptance Criteria:** Concurrent Logins do not crash the server.
    - **Labels:** `CORE`, `THREAD`

- [ ] **[NET] Implement Broadcast & Direct Messaging**
    - **Description:** Logic to route messages between sessions.
    - **Technical Details:**
        - **Direct:** Lookup -> Enqueue Write Task.
        - **Broadcast:** Iterate Registry -> Enqueue Write Tasks.
    - **Acceptance Criteria:** Client A can send text to Client B.
    - **Labels:** `NET`

- [ ] **[NET] Implement Heartbeat (Keep-Alive)**
    - **Description:** Disconnect idle clients.
    - **Technical Details:**
        - Client sends PING, Server sends PONG.
        - Server checks `last_activity` timestamp periodically.
    - **Acceptance Criteria:** Inactive client is forcefully disconnected after N seconds.
    - **Labels:** `NET`, `OPS`

- [ ] **[CORE] Implement Graceful Shutdown**
    - **Description:** Handle SIGINT/SIGTERM.
    - **Technical Details:**
        - `signalfd` integrated into Epoll.
        - Stop Accept -> Wait for Workers -> Close Sockets.
    - **Acceptance Criteria:** App exits with code 0 without leaks.
    - **Labels:** `CORE`, `OPS`

---

## Phase 5: Performance & Final Polish
*Validation*

- [ ] **[TEST] Build Python Load Generator**
    - **Description:** Script to simulate 1000+ clients.
    - **Technical Details:**
        - Python `asyncio`.
        - Implements binary handshake + random msg spam.
    - **Acceptance Criteria:** Server handles 1000 concurrent connections.
    - **Labels:** `TEST`

- [ ] **[DOC] Profiling & Optimization**
    - **Description:** Identify bottlenecks.
    - **Technical Details:**
        - Use `perf`, `FlameGraph`.
        - Measure Messages Per Second (MPS).
    - **Acceptance Criteria:** Performance graphs generated.
    - **Labels:** `DOC`

- [ ] **[DOC] Generate Doxygen Documentation**
    - **Description:** API documentation.
    - **Technical Details:**
        - Annotate headers.
        - Generate HTML.
    - **Acceptance Criteria:** Full HTML docs available.
    - **Labels:** `DOC`
