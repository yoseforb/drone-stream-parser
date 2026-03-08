# Phase 5: Server Infrastructure — Implementation Plan

**Date:** 2026-03-08
**Status:** Active
**Depends on:** Phase 2 (domain), Phase 3 (protocol), Phase 4 (common)
**Test strategy:** No unit tests. Integration tested via client binary (Phase 6).

---

## Overview

Phase 5 wires the full server binary: it implements the two domain port adapters
(InMemoryDroneRepository, ConsoleAlertNotifier), the OS-level infrastructure
(SignalHandler, TcpServer), and the composition root (main.cpp) that creates all
objects, injects dependencies, spawns the three pipeline threads, and orchestrates
graceful shutdown.

After this phase, `drone_server` starts, accepts connections on a configurable port,
processes telemetry through the 3-stage pipeline, fires alerts to the console, and
shuts down cleanly on SIGINT or SIGTERM.

**Order constraint:** 5a → 5b → 5c → 5d → 5e. The composition root (5e) is always
last because it depends on all preceding components.

---

## Decision: InMemoryDroneRepository Thread Safety

The architecture doc (section 4) states the repository requires **no mutex** because
only Stage 3 (the process thread) ever accesses it — the single-consumer pipeline
guarantee. The master plan specifies a `std::mutex` as a more conservative choice.

**Resolution: No mutex.** The single-consumer guarantee is a structural property of
the pipeline, not a runtime invariant that can be violated. Adding a mutex would be
defensive over-engineering for a guarantee already enforced by the data flow design
(ADR-006). The repository is constructed in main(), passed by reference to
ProcessTelemetry, and ProcessTelemetry is called exclusively from the process thread.
No other thread holds a reference to the repository.

If the pipeline structure ever changes (e.g., two process threads), the architectural
decision must be revisited — but the right fix is to reconsider the concurrency model,
not to preemptively add mutex contention now.

Document this in a code comment in `in_memory_drone_repo.hpp`.

---

## Task 5a: SignalHandler

**Story Points:** 2

### Files

- `server/include/signal_handler.hpp` (new)
- `server/src/signal_handler.cpp` (new)
- `server/CMakeLists.txt` (update — add `signal_handler.cpp` source)

### Header: `server/include/signal_handler.hpp`

```cpp
#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

#include <atomic>

class SignalHandler {
public:
  explicit SignalHandler(std::atomic<bool>& stop_flag);

private:
  // No data members needed beyond the file-scope pointer (see .cpp).
  // POSIX signal handlers must be plain C functions; they cannot capture
  // object state. A file-scope pointer is the standard solution.
};

#endif // SIGNAL_HANDLER_HPP
```

### Implementation: `server/src/signal_handler.cpp`

- `#include` headers: `signal_handler.hpp`, `<atomic>`, `<csignal>`, `<cerrno>`
- Declare a file-scope (anonymous namespace) `std::atomic<bool>* g_stop_flag{nullptr}`
- Declare a plain C signal handler function:
  ```cpp
  namespace {
  std::atomic<bool>* g_stop_flag{nullptr};
  void handle_signal(int /*sig*/) {
    if (g_stop_flag != nullptr) {
      g_stop_flag->store(true, std::memory_order_relaxed);
    }
  }
  } // namespace
  ```
- Constructor body:
  1. Assign `g_stop_flag = &stop_flag`
  2. Declare `struct sigaction sa{}`
  3. Set `sa.sa_handler = handle_signal`
  4. Call `sigemptyset(&sa.sa_mask)`
  5. Set `sa.sa_flags = 0`
  6. Call `sigaction(SIGINT, &sa, nullptr)`
  7. Call `sigaction(SIGTERM, &sa, nullptr)`
  - No error checking needed — `sigaction` only fails on invalid signal numbers
    (which SIGINT and SIGTERM are not) or null `sa` pointer (which we don't pass).

### Naming

- Class: `SignalHandler`
- Member: none (beyond the file-scope pointer)
- Signal handler function: `handle_signal` (in anonymous namespace)
- File-scope pointer: `g_stop_flag` (in anonymous namespace)

### CMakeLists.txt update

```cmake
add_executable(drone_server
  src/signal_handler.cpp
  src/main.cpp
)
```

At the end of phase 5, the full source list will be built incrementally. Add each
`.cpp` file as it is implemented. For 5a, update from:

```cmake
add_executable(drone_server src/main.cpp)
```

to:

```cmake
add_executable(drone_server
  src/signal_handler.cpp
  src/main.cpp
)
```

### Verification

After implementing 5a:

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds (existing tests still green). `drone_server` binary links
and runs (the current stub main.cpp returns 0 immediately — that is acceptable here).

---

## Task 5b: InMemoryDroneRepository

**Story Points:** 2

### Files

- `server/include/in_memory_drone_repo.hpp` (new)
- `server/src/in_memory_drone_repo.cpp` (new)
- `server/CMakeLists.txt` (add `in_memory_drone_repo.cpp`)

### Header: `server/include/in_memory_drone_repo.hpp`

```cpp
#ifndef IN_MEMORY_DRONE_REPO_HPP
#define IN_MEMORY_DRONE_REPO_HPP

#include <cstddef>
#include <optional>
#include <string>
#include <unordered_map>

#include "drone.hpp"
#include "i_drone_repository.hpp"

// Thread safety: no mutex. This repository is accessed exclusively from the
// process thread (Stage 3 of the pipeline). The single-consumer guarantee is
// structural — enforced by the data flow design (ADR-006), not a runtime
// invariant that requires locking.
class InMemoryDroneRepository : public IDroneRepository {
public:
  std::optional<Drone> findById(const std::string& drone_id) override;
  void save(const Drone& drone) override;

  [[nodiscard]] std::size_t size() const;

private:
  std::unordered_map<std::string, Drone> drones_;
};

#endif // IN_MEMORY_DRONE_REPO_HPP
```

Note the `size()` getter — required for the shutdown summary log in 5e (master plan
explicitly calls this out: "add this getter to InMemoryDroneRepository").

### Implementation: `server/src/in_memory_drone_repo.cpp`

- `#include` headers: `in_memory_drone_repo.hpp`, `<optional>`, `<string>`
- `findById`:
  - `auto it = drones_.find(drone_id);`
  - `if (it == drones_.end()) return std::nullopt;`
  - `return it->second;` (returns copy — caller may mutate the returned Drone)
- `save`:
  - `drones_.insert_or_assign(drone.getId(), drone);`
  - Upserts: inserts if not present, overwrites if present. This matches the
    `FakeDroneRepository` pattern already established in the test fakes.
- `size`:
  - `return drones_.size();`

### CMakeLists.txt update

Add `src/in_memory_drone_repo.cpp` to the `drone_server` source list.

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds, existing tests green.

---

## Task 5c: ConsoleAlertNotifier

**Story Points:** 2

### Files

- `server/include/console_alert_notifier.hpp` (new)
- `server/src/console_alert_notifier.cpp` (new)
- `server/CMakeLists.txt` (add `console_alert_notifier.cpp`)

### Header: `server/include/console_alert_notifier.hpp`

```cpp
#ifndef CONSOLE_ALERT_NOTIFIER_HPP
#define CONSOLE_ALERT_NOTIFIER_HPP

#include <string>
#include <vector>

#include "alert_types.hpp"
#include "i_alert_notifier.hpp"

class ConsoleAlertNotifier : public IAlertNotifier {
public:
  void notify(const std::string& drone_id,
              const std::vector<AlertTransition>& transitions) override;
};

#endif // CONSOLE_ALERT_NOTIFIER_HPP
```

### Implementation: `server/src/console_alert_notifier.cpp`

- `#include` headers: `console_alert_notifier.hpp`, `<string>`, `<vector>`,
  `"alert_types.hpp"`, `<spdlog/spdlog.h>`
- `notify` body: iterate `transitions`, emit one `spdlog::warn(...)` per transition:
  ```cpp
  void ConsoleAlertNotifier::notify(const std::string& drone_id,
                                    const std::vector<AlertTransition>& transitions) {
    for (const auto& t : transitions) {
      const char* type_str =
          (t.type == AlertType::ALTITUDE) ? "ALTITUDE" : "SPEED";
      const char* state_str = t.entered ? "ENTERED" : "CLEARED";
      spdlog::warn("[ALERT] drone={} type={} state={}", drone_id, type_str, state_str);
    }
  }
  ```
- Log level: `spdlog::warn` — alerts are warning-level events, not errors (the drone
  is operating outside limits, not that the system has failed).
- One log line per `AlertTransition`, not one line per `notify()` call. This makes
  each alert individually visible in the log.

### AlertType to string mapping

Use a simple ternary or `if/else` — no map, no switch with string table. There are
only two alert types and they will not change during Phase 5. If a third type is added
later, the compiler will warn on a non-exhaustive if the switch uses `default`-less
coverage (clang-tidy will catch it). For now, a ternary is the simplest correct thing.

### CMakeLists.txt update

Add `src/console_alert_notifier.cpp` to the `drone_server` source list.

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds, existing tests green.

---

## Task 5d: TcpServer

**Story Points:** 5

### Files

- `server/include/tcp_server.hpp` (new)
- `server/src/tcp_server.cpp` (new)
- `server/CMakeLists.txt` (add `tcp_server.cpp`)

### Header: `server/include/tcp_server.hpp`

```cpp
#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "blocking_queue.hpp"

class TcpServer {
public:
  TcpServer(uint16_t port, BlockingQueue<std::vector<uint8_t>>& queue,
            std::atomic<bool>& stop_flag);
  ~TcpServer();

  // Not copyable or movable — owns a file descriptor.
  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;
  TcpServer(TcpServer&&) = delete;
  TcpServer& operator=(TcpServer&&) = delete;

  // Blocking accept loop. Call from the recv thread. Returns when stop_flag
  // is set (after closing server socket and calling queue.close()).
  void run();

private:
  BlockingQueue<std::vector<uint8_t>>& queue_;
  std::atomic<bool>& stop_flag_;
  int server_fd_;
};

#endif // TCP_SERVER_HPP
```

Design notes:
- Constructor opens and binds the socket. If binding fails, it throws `std::runtime_error`.
  Opening in the constructor means the port is claimed at object creation time, before
  `run()` is called. This allows main() to log "listening on port X" before spawning
  threads, with a guarantee that the port is actually bound.
- Destructor closes `server_fd_` if it is still open (defensive — `run()` closes it on
  normal shutdown, but destructor guards against exception paths).
- The `int server_fd_` member is initialized to `-1` as sentinel for "closed".

### Implementation: `server/src/tcp_server.cpp`

#### Headers to include

```cpp
#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <cerrno>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

#include <spdlog/spdlog.h>
```

#### Constructor

```
1. server_fd_ = socket(AF_INET, SOCK_STREAM, 0)
   - If < 0: throw std::runtime_error("socket() failed: " + strerror(errno))

2. setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt))
   where opt = 1 (int)
   - Allows immediate rebind after server restart (avoids TIME_WAIT)
   - If < 0: close(server_fd_); throw std::runtime_error(...)

3. struct sockaddr_in addr{}
   addr.sin_family = AF_INET
   addr.sin_addr.s_addr = INADDR_ANY
   addr.sin_port = htons(port)

4. bind(server_fd_, reinterpret_cast<sockaddr*>(&addr), sizeof(addr))
   - If < 0: close(server_fd_); throw std::runtime_error("bind() failed: " + strerror(errno))

5. listen(server_fd_, /*backlog=*/5)
   - If < 0: close(server_fd_); throw std::runtime_error("listen() failed: " + strerror(errno))

6. spdlog::info("TcpServer: listening on port {}", port)
```

#### Destructor

```cpp
TcpServer::~TcpServer() {
  if (server_fd_ >= 0) {
    close(server_fd_);
    server_fd_ = -1;
  }
}
```

#### `run()` — Accept Loop

The loop uses `poll()` with a short timeout (200ms) on `server_fd_` to check
`stop_flag_` responsively without busy-waiting.

```
while (!stop_flag_) {
  // poll() with 200ms timeout — check stop_flag_ between polls
  struct pollfd pfd{server_fd_, POLLIN, 0}
  int ready = poll(&pfd, 1, 200)  // 200ms timeout

  if (ready < 0) {
    if (errno == EINTR) continue   // interrupted by signal, re-check stop_flag_
    break                          // unexpected error
  }
  if (ready == 0) continue         // timeout, re-check stop_flag_

  // accept() — will not block because poll() says POLLIN is ready
  struct sockaddr_in client_addr{}
  socklen_t client_len = sizeof(client_addr)
  int client_fd = accept(server_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_len)
  if (client_fd < 0) {
    if (errno == EINTR || errno == EAGAIN) continue
    break
  }

  // Format client IP for logging
  char ip_str[INET_ADDRSTRLEN]
  inet_ntop(AF_INET, &client_addr.sin_addr, ip_str, sizeof(ip_str))
  spdlog::info("TcpServer: client connected from {}", ip_str)

  // Per-client recv loop
  recvLoop(client_fd)   // see below

  close(client_fd)
  spdlog::info("TcpServer: client disconnected, returning to accept()")
}

// Shutdown path
close(server_fd_)
server_fd_ = -1
queue_.close()
spdlog::info("TcpServer: shut down")
```

#### `recvLoop()` — private helper

Declare as private in the header:

```cpp
void recvLoop(int client_fd);
```

Add to `tcp_server.hpp` private section.

Implementation:

```
static constexpr std::size_t RecvBufSize = 4096

std::array<uint8_t, RecvBufSize> buf

while (!stop_flag_) {
  ssize_t n = recv(client_fd, buf.data(), buf.size(), 0)

  if (n < 0) {
    if (errno == EINTR) continue
    spdlog::warn("TcpServer: recv() error: {}", strerror(errno))
    return
  }
  if (n == 0) {
    // Client closed the connection (graceful EOF)
    return
  }

  std::vector<uint8_t> chunk(buf.begin(), buf.begin() + n)
  queue_.push(std::move(chunk))
}
```

When `stop_flag_` becomes true mid-recv-loop, `recv()` may block waiting for data.
The recv loop checks `stop_flag_` only between `recv()` calls. This is acceptable
because:
- The signal sets `stop_flag_` atomically.
- On Linux, SIGINT/SIGTERM interrupts blocking syscalls with `EINTR`.
- The `EINTR` branch checks `stop_flag_` and returns.
- `SO_RCVTIMEO` is an alternative but adds complexity; `EINTR` is sufficient here.

#### `recvLoop` private member — update header

The final `tcp_server.hpp` private section:

```cpp
private:
  BlockingQueue<std::vector<uint8_t>>& queue_;
  std::atomic<bool>& stop_flag_;
  int server_fd_{-1};

  void recvLoop(int client_fd);
```

### CMakeLists.txt update

Add `src/tcp_server.cpp` to the `drone_server` source list.

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds, existing tests green.

---

## Task 5e: Composition Root — main.cpp

**Story Points:** 3

### Files

- `server/src/main.cpp` (replace stub with full implementation)
- `server/CMakeLists.txt` (verify all sources are listed — no new sources here)

### Final CMakeLists.txt for `drone_server`

```cmake
find_package(Threads REQUIRED)

add_executable(drone_server
  src/signal_handler.cpp
  src/in_memory_drone_repo.cpp
  src/console_alert_notifier.cpp
  src/tcp_server.cpp
  src/main.cpp
)

target_include_directories(drone_server PRIVATE include)
target_compile_features(drone_server PRIVATE cxx_std_20)
target_link_libraries(drone_server PRIVATE
  protocol domain common spdlog::spdlog Threads::Threads
)
```

Note: the current `server/CMakeLists.txt` already has the correct `target_link_libraries`
line. The only change across phases 5a–5d is incrementally adding `.cpp` sources. By 5e,
`main.cpp` is already listed (it was there as the stub). Ensure all five sources appear.

### `server/src/main.cpp` — Full Implementation

#### Includes

```cpp
#include <atomic>
#include <cstdlib>
#include <string>
#include <thread>

#include <spdlog/spdlog.h>

#include "alert_policy.hpp"
#include "blocking_queue.hpp"
#include "console_alert_notifier.hpp"
#include "in_memory_drone_repo.hpp"
#include "process_telemetry.hpp"
#include "signal_handler.hpp"
#include "stream_parser.hpp"
#include "tcp_server.hpp"
#include "telemetry.hpp"
```

#### CLI Argument Parsing

Parse `--port <number>` from `argv`. Default port: 9000.

```cpp
static uint16_t parsePort(int argc, char** argv) {
  for (int i = 1; i + 1 < argc; ++i) {
    if (std::string(argv[i]) == "--port") {
      return static_cast<uint16_t>(std::stoi(argv[i + 1]));
    }
  }
  return 9000;
}
```

Place this as a `static` function in the anonymous namespace at the top of `main.cpp`,
before `main()`.

#### `main()` Body — Step by Step

```
1. Parse port
   uint16_t port = parsePort(argc, argv)

2. Construct domain + port adapters
   AlertPolicy policy{}
   InMemoryDroneRepository repo{}
   ConsoleAlertNotifier notifier{}
   ProcessTelemetry use_case{repo, notifier, policy}

3. Construct queues
   BlockingQueue<std::vector<uint8_t>> q1{256}
   BlockingQueue<Telemetry> q2{256}

4. Construct infrastructure
   std::atomic<bool> stop_flag{false}
   SignalHandler signal_handler{stop_flag}
   TcpServer server{port, q1, stop_flag}
     // TcpServer constructor binds the socket — throws on failure

5. Log startup
   spdlog::info("drone_server starting on port {}. altitude_limit={:.1f}m speed_limit={:.1f}m/s",
                port, policy.altitude_limit, policy.speed_limit)

6. Construct parser with Q2 push callback
   // The callback must not throw — BlockingQueue::push does not throw.
   StreamParser parser{[&q2](Telemetry t) { q2.push(std::move(t)); }}

7. Packet counter (for shutdown summary)
   std::uint64_t packets_processed{0}

8. Spawn three pipeline threads

   recv thread:
   std::thread recv_thread{[&server]() { server.run(); }}

   parse thread:
   std::thread parse_thread{[&q1, &parser, &q2]() {
     while (auto chunk = q1.pop()) {
       parser.feed(std::span<const uint8_t>{chunk->data(), chunk->size()});
     }
     q2.close();
   }}

   process thread:
   std::thread process_thread{[&q2, &use_case, &packets_processed]() {
     while (auto t = q2.pop()) {
       use_case.execute(*t);
       ++packets_processed;
     }
   }}

9. Join in pipeline order (process → parse → recv ensures each stage drains before
   its upstream is joined, but technically any join order works here because the
   cascade guarantees correct termination before main() continues)
   process_thread.join()
   parse_thread.join()
   recv_thread.join()

10. Log shutdown summary
    spdlog::info("Shutdown complete. packets_processed={} crc_failures={} malformed={} active_drones={}",
                 packets_processed,
                 parser.getCrcFailCount(),
                 parser.getMalformedCount(),
                 repo.size())

11. return 0
```

#### Notes on the implementation

- `packets_processed` is a plain `uint64_t` captured by reference in the process
  thread lambda. It is only ever written by that one thread and only read by main()
  after `process_thread.join()`. No atomic needed.

- `use_case.execute(*t)` may throw if the port implementations throw. Wrap in a
  try-catch in the process thread if robustness is needed — but for Phase 5, let
  exceptions propagate to `std::terminate`. The domain implementations
  (`InMemoryDroneRepository::save`, `ConsoleAlertNotifier::notify`) do not throw
  in practice (in-memory storage, spdlog does not throw). Leaving the process thread
  without a try-catch keeps the code simple and correct for the specified use case.

- `std::span` constructor in parse thread: `parser.feed` takes
  `std::span<const uint8_t>`. Construct it from `chunk->data()` and `chunk->size()`
  to match the signature in `stream_parser.hpp`.

- The `StreamParser` is constructed before the threads are spawned. It is only ever
  accessed from the parse thread — no thread safety issues.

- `TcpServer` constructor may throw `std::runtime_error` (e.g., port already in use).
  This propagates out of `main()` which calls `std::terminate`. This is acceptable
  for a server binary — an unrecoverable startup failure should crash with an error.
  Do not catch this exception here; let the OS print the uncaught exception message
  or add a try-catch around the entire main body that logs and returns EXIT_FAILURE
  if desired. Keep it simple: no try-catch for Phase 5.

### Shutdown Cascade Trace

For documentation clarity, the full shutdown sequence triggered by Ctrl-C:

```
1. OS delivers SIGINT to the process
2. handle_signal() sets g_stop_flag->store(true)
3. stop_flag_ is true
4. recv thread: TcpServer::run() checks stop_flag_ on next poll() timeout (≤200ms)
   → exits accept loop → close(server_fd_) → queue_.close() → returns from run()
   → recv_thread exits
5. parse thread: q1.pop() returns std::nullopt (queue closed)
   → while loop exits → q2.close() → parse_thread exits
6. process thread: q2.pop() returns std::nullopt (queue closed)
   → while loop exits → process_thread exits
7. main(): process_thread.join() returns → parse_thread.join() returns
   → recv_thread.join() returns
8. Shutdown summary logged
9. main() returns 0
```

The cascade is deterministic. No data is silently dropped — each stage drains
its input queue to empty before the downstream queue is closed.

### Verification

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds, all existing tests green.

Then manually test:

```bash
./build/dev/drone_server --port 9000
```

Expected console output (approximate):
```
[info] TcpServer: listening on port 9000
[info] drone_server starting on port 9000. altitude_limit=120.0m speed_limit=50.0m/s
```

Press Ctrl-C (≤200ms response):
```
[info] TcpServer: shut down
[info] Shutdown complete. packets_processed=0 crc_failures=0 malformed=0 active_drones=0
```

Test with a non-default port:
```bash
./build/dev/drone_server --port 8080
```

Expected: `[info] drone_server starting on port 8080. ...`

---

## Full CMakeLists.txt State After Phase 5

```cmake
find_package(Threads REQUIRED)

add_executable(drone_server
  src/signal_handler.cpp
  src/in_memory_drone_repo.cpp
  src/console_alert_notifier.cpp
  src/tcp_server.cpp
  src/main.cpp
)

target_include_directories(drone_server PRIVATE include)
target_compile_features(drone_server PRIVATE cxx_std_20)
target_link_libraries(drone_server PRIVATE
  protocol domain common spdlog::spdlog Threads::Threads
)
```

This is identical to the existing `server/CMakeLists.txt` except for the additional
source files. The `find_package(Threads REQUIRED)` call already exists in the root
`CMakeLists.txt` as well — the one in `server/CMakeLists.txt` is harmless but
redundant; leave it as-is to avoid unnecessary churn.

---

## File Summary

| File | Action |
|------|--------|
| `server/include/signal_handler.hpp` | Create |
| `server/src/signal_handler.cpp` | Create |
| `server/include/in_memory_drone_repo.hpp` | Create |
| `server/src/in_memory_drone_repo.cpp` | Create |
| `server/include/console_alert_notifier.hpp` | Create |
| `server/src/console_alert_notifier.cpp` | Create |
| `server/include/tcp_server.hpp` | Create |
| `server/src/tcp_server.cpp` | Create |
| `server/src/main.cpp` | Replace stub |
| `server/CMakeLists.txt` | Update (add sources incrementally) |

No other files are modified. Domain, protocol, and common boundaries are untouched.

---

## Clang-Tidy Considerations

The following patterns may trigger clang-tidy and need handling:

- `reinterpret_cast` in `tcp_server.cpp` for `sockaddr*` casts: use
  `// NOLINT(cppcoreguidelines-pro-type-reinterpret-cast)` inline if the linter
  flags it. This cast is unavoidable for POSIX socket API.
- `int server_fd_` storing a POSIX file descriptor: clang-tidy may flag it as a
  "magic number" or suggest `gsl::owner<int>`. Add `// NOLINT` if needed.
- `static` file-scope pointer in `signal_handler.cpp`: acceptable for POSIX signal
  handler constraint. Use anonymous namespace rather than `static`.
- Array size constants (e.g., `RecvBufSize = 4096`): define as `inline constexpr`
  in the anonymous namespace, not as raw integer literals in function bodies.
- All `errno`-checking code: use `strerror(errno)` immediately after the failing call
  (before any other function that could change errno).
- `INET_ADDRSTRLEN`: `char ip_str[INET_ADDRSTRLEN]` — clang-tidy may flag C-style
  arrays. If so, use `std::array<char, INET_ADDRSTRLEN> ip_str{}`.

---

## Total Story Points: 14

| Task | Points |
|------|--------|
| 5a: SignalHandler | 2 |
| 5b: InMemoryDroneRepository | 2 |
| 5c: ConsoleAlertNotifier | 2 |
| 5d: TcpServer | 5 |
| 5e: Composition Root | 3 |
