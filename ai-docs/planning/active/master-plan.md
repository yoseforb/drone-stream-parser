# Drone Stream Parser — Master Implementation Plan

**Date:** 2026-03-07
**Status:** Active
**Standard:** C++20 | GCC 15.2.1 | CMake 4.2.3 | Linux

---

## Overview

Multi-threaded C++ streaming parser for drone telemetry. 3-stage pipeline: TCP recv
→ stream parse → domain process. Architecture uses 3 pragmatic boundaries (Domain,
Protocol, Infrastructure) enforced at build time via CMake link dependencies.

**Workflow per component:** interface → stub (fails) → test (RED) → implementation (GREEN) → refactor (GREEN)

**Dependency direction:** Infrastructure → Protocol → Domain. Common available to all.

---

## Phase 1: Project Scaffolding

Goal: `cmake --build build` succeeds with empty targets.

### Root CMakeLists.txt

- [ ] `cmake_minimum_required(VERSION 3.20)`, `project(drone-stream-parser CXX)`, `set(CMAKE_CXX_STANDARD 20)`, `CMAKE_CXX_STANDARD_REQUIRED ON`
- [ ] Enable compiler warnings: `-Wall -Wextra -Wpedantic -Werror` for GCC
- [ ] `add_subdirectory()` calls for: `domain`, `protocol`, `common`, `server`, `client`, `tests`
- [ ] FetchContent block for GTest+GMock — pin to stable tag (e.g., `v1.14.0`)
- [ ] FetchContent block for spdlog — compiled mode, pin to stable tag (e.g., `v1.14.1`)

### Subdirectory Stubs

Each subdirectory needs a `CMakeLists.txt` that declares the target with no sources initially:

- [ ] `domain/CMakeLists.txt` — `add_library(domain STATIC)`, `target_include_directories(domain PUBLIC include)`
- [ ] `protocol/CMakeLists.txt` — `add_library(protocol STATIC)`, links `domain`
- [ ] `common/CMakeLists.txt` — `add_library(common INTERFACE)`, `target_include_directories(common INTERFACE include)`
- [ ] `server/CMakeLists.txt` — `add_executable(drone_server)`, links `protocol domain common Threads::Threads spdlog::spdlog`
- [ ] `client/CMakeLists.txt` — `add_executable(drone_client)`, links `protocol domain spdlog::spdlog`
- [ ] `tests/CMakeLists.txt` — `add_executable(tests)`, links `protocol domain GTest::gtest_main GTest::gmock`

### Tooling

- [ ] `.clang-tidy` — enable `modernize-*`, `readability-*`, `performance-*`, `bugprone-*` checks; disable `modernize-use-trailing-return-type`
- [ ] Integrate clang-tidy into CMake: `set(CMAKE_CXX_CLANG_TIDY clang-tidy)` (off by default, opt-in via `-DENABLE_CLANG_TIDY=ON`)
- [ ] `.clang-format` — Google or LLVM base style, `ColumnLimit: 80`, `PointerAlignment: Left`
- [ ] Integrate clang-format: add `format` custom CMake target that runs `clang-format -i` on all sources
- [ ] `CLAUDE.md` at project root with: build command, test command, lint command, format command, workflow summary

### Verify

- [ ] `cmake -S . -B build && cmake --build build` exits 0 with no source files yet

---

## Phase 2: Domain Boundary

**Zero external dependencies. Fully TDD'd. Order matters — each item depends on what is above it.**

### 2a: Value Objects (header-only)

- [ ] `domain/include/telemetry.hpp` — `struct Telemetry { std::string drone_id; double latitude, longitude, altitude, speed; uint64_t timestamp; };`
- [ ] `domain/include/alert_types.hpp` — `enum class AlertType { ALTITUDE, SPEED };` and `struct AlertTransition { AlertType type; bool entered; };`
- [ ] `domain/include/alert_policy.hpp` — `struct AlertPolicy { double altitude_limit = 120.0; double speed_limit = 50.0; };` with `constexpr` defaults

### 2b: Port Interfaces (pure abstract, header-only)

- [ ] `domain/include/i_drone_repository.hpp` — pure abstract class:
  - `virtual std::optional<Drone> findById(const std::string&) = 0;`
  - `virtual void save(const Drone&) = 0;`
  - `virtual ~IDroneRepository() = default;`
- [ ] `domain/include/i_alert_notifier.hpp` — pure abstract class:
  - `virtual void notify(const std::string& drone_id, const std::vector<AlertTransition>&) = 0;`
  - `virtual ~IAlertNotifier() = default;`

### 2c: Test Fakes

- [ ] `tests/domain/fakes/fake_drone_repository.hpp` — implements `IDroneRepository` with `std::unordered_map`; `findById` returns stored drone or `std::nullopt`; `save` stores by id
- [ ] `tests/domain/fakes/fake_alert_notifier.hpp` — implements `IAlertNotifier`; records all calls to `notify` in a `std::vector` for test assertions

### 2d: Drone Entity

Interface first, then stub, then test (RED), then implementation (GREEN):

- [ ] `domain/include/drone.hpp` — class declaration:
  - Constructor: `Drone(std::string drone_id)`
  - `const std::string& getId() const noexcept`
  - `const std::set<AlertType>& getAlertState() const noexcept`
  - `std::vector<AlertTransition> updateFrom(const Telemetry&, const AlertPolicy&) noexcept`
- [ ] `domain/src/drone.cpp` — stub: `updateFrom` returns `{}`
- [ ] `tests/domain/drone_test.cpp` — RED tests:
  - New drone has empty alert state
  - `updateFrom` with altitude above limit enters `ALTITUDE` alert, returns `AlertTransition{ALTITUDE, true}`
  - `updateFrom` with altitude back below limit clears alert, returns `AlertTransition{ALTITUDE, false}`
  - Speed alert enters and clears correctly
  - No transition returned when state does not change (already in alert, stays in alert)
  - Both alerts can be active simultaneously
- [ ] `domain/src/drone.cpp` — GREEN implementation: evaluate each `AlertType` against `AlertPolicy`, diff against `active_alerts_` set, update set, return transitions

### 2e: Use Case — ProcessTelemetry

Interface first, then stub, then test (RED), then implementation (GREEN):

- [ ] `domain/include/process_telemetry.hpp` — class declaration:
  - Constructor: `ProcessTelemetry(IDroneRepository&, IAlertNotifier&, AlertPolicy)`
  - `void execute(const Telemetry&)` — not noexcept (propagates port exceptions)
- [ ] `domain/src/process_telemetry.cpp` — stub: `execute` is a no-op
- [ ] `tests/domain/process_telemetry_test.cpp` — RED tests using fakes:
  - New drone created and saved on first telemetry
  - Existing drone retrieved, updated, saved
  - Alert notifier called only on transitions (entered or cleared)
  - Alert notifier NOT called when no transitions occur
  - Multiple transitions in one update all forwarded to notifier
- [ ] `domain/src/process_telemetry.cpp` — GREEN implementation:
  1. `repo.findById(telemetry.drone_id)` → optional
  2. Construct new `Drone` if nullopt
  3. `drone.updateFrom(telemetry, policy_)` → transitions
  4. `repo.save(drone)`
  5. If transitions non-empty → `notifier.notify(drone_id, transitions)`
- [ ] Update `domain/CMakeLists.txt` to include `drone.cpp` and `process_telemetry.cpp` as sources
- [ ] Update `tests/CMakeLists.txt` to include domain test files; add `enable_testing()` and `add_test()`

**Verify:** `cmake --build build && ./build/tests` — all domain tests GREEN.

---

## Phase 3: Protocol Boundary

**Depends on Domain (Telemetry type). Fully TDD'd. Order matters.**

### 3a: CRC16

- [ ] `protocol/include/crc16.hpp` — function declaration: `uint16_t crc16(std::span<const uint8_t> data) noexcept;`
- [ ] `protocol/src/crc16.cpp` — stub: returns `0`
- [ ] `tests/protocol/crc16_test.cpp` — RED tests:
  - Empty span → known value (0x0000 for CRC-16/CCITT with init 0x0000)
  - Known byte sequence → known precomputed CRC value (use an online CRC calculator for the fixture)
  - Single byte → known value
  - Verify polynomial 0x1021, init 0x0000 (no input/output reflection)
- [ ] `protocol/src/crc16.cpp` — GREEN implementation: 256-entry lookup table, CRC-16/CCITT (poly 0x1021, init 0x0000, no reflection)
- [ ] Update `protocol/CMakeLists.txt` to include `crc16.cpp`

### 3b: PacketSerializer

Wire format: `[0xAA 0x55] [LENGTH:u16 LE] [PAYLOAD:N bytes] [CRC:u16 LE]`
Payload: `[id_len:u16 LE] [drone_id bytes] [lat:f64 LE] [lon:f64 LE] [alt:f64 LE] [speed:f64 LE] [timestamp:u64 LE]`

- [ ] `protocol/include/packet_serializer.hpp` — class declaration:
  - `std::vector<uint8_t> serialize(const Telemetry&)` — not noexcept (may throw `std::bad_alloc`)
- [ ] `protocol/src/packet_serializer.cpp` — stub: returns `{}`
- [ ] `tests/protocol/serializer_test.cpp` — RED tests:
  - Serialized output starts with `0xAA, 0x55`
  - LENGTH field (bytes 2-3) equals payload byte count as uint16_t LE
  - Payload begins with drone_id length as uint16_t LE, followed by drone_id bytes
  - Numeric fields are little-endian IEEE 754 doubles and uint64_t
  - CRC at end matches `crc16()` over header+length+payload
  - Round-trip: serialize then deserialize (via parser) recovers original Telemetry
- [ ] `protocol/src/packet_serializer.cpp` — GREEN implementation: `memcpy`-based little-endian serialization, calls `crc16()`, prepends header and length
- [ ] Update `protocol/CMakeLists.txt` to include `packet_serializer.cpp`

### 3c: StreamParser

4-state machine: `HUNT_HEADER → READ_LENGTH → READ_PAYLOAD → READ_CRC → HUNT_HEADER`

- [ ] `protocol/include/stream_parser.hpp` — class declaration:
  - Constructor: `StreamParser(std::function<void(Telemetry)> callback)` — callback must not throw
  - `void feed(std::span<const uint8_t> data) noexcept` — appends to internal buffer, runs state machine
  - `uint64_t getCrcFailCount() const noexcept`
  - `uint64_t getMalformedCount() const noexcept`
  - Internal: `std::vector<uint8_t> buffer_`, `size_t read_pos_`, `State state_`, `size_t header_start_`
- [ ] `protocol/src/stream_parser.cpp` — stub: `feed` is a no-op
- [ ] `tests/protocol/parser_test.cpp` — RED tests:
  - Single valid packet → callback called once with correct Telemetry
  - Valid packet split across two `feed()` calls → callback called once
  - Valid packet split into 1-byte `feed()` calls → callback called once
  - Two valid packets in one `feed()` call → callback called twice
  - Garbage bytes before valid packet → parser resyncs, callback called once
  - Packet with bad CRC → callback not called, `getCrcFailCount() == 1`
  - Packet with LENGTH > 4096 → callback not called, `getMalformedCount() == 1`, no crash
  - Bad CRC packet followed by valid packet → only valid packet emitted
  - Garbage + valid + garbage + valid → both valid packets emitted in order
- [ ] `protocol/src/stream_parser.cpp` — GREEN implementation:
  - Internal `std::vector<uint8_t> buffer_` accumulates all fed bytes
  - `size_t read_pos_` tracks current parse position within buffer
  - `size_t header_start_` records position of the `0xAA` byte for rewind
  - State machine dispatches on `state_` enum per iteration
  - `HUNT_HEADER`: scan for `0xAA` then `0x55`; record `header_start_`
  - `READ_LENGTH`: read 2 bytes LE into `pending_length_`; if > 4096: increment `malformed_count_`, rewind to `header_start_ + 1`, re-enter `HUNT_HEADER`
  - `READ_PAYLOAD`: accumulate `pending_length_` bytes
  - `READ_CRC`: read 2 bytes LE; compute `crc16()` over `buffer_[header_start_..read_pos_-2]`; if mismatch: increment `crc_fail_count_`, rewind to `header_start_ + 1`, re-enter `HUNT_HEADER`; if match: deserialize Telemetry, invoke callback, erase consumed prefix from buffer, reset `read_pos_`
- [ ] Update `protocol/CMakeLists.txt` to include `stream_parser.cpp`
- [ ] Update `tests/CMakeLists.txt` to include protocol test files

**Verify:** `cmake --build build && ./build/tests` — all domain and protocol tests GREEN.

---

## Phase 4: Common Utility

**Server-only. TDD'd in isolation.**

### BlockingQueue

- [ ] `common/include/blocking_queue.hpp` — header-only template class:
  - `template<typename T> class BlockingQueue`
  - Constructor: `explicit BlockingQueue(size_t capacity)`
  - `void push(T&& item)` — blocks when full; returns immediately if closed
  - `std::optional<T> pop()` — blocks when empty; returns `std::nullopt` when closed and empty
  - `void close()` — signals shutdown; unblocks all waiting threads
  - Internal: `std::deque<T>`, `std::mutex`, `std::condition_variable`, `bool closed_`, `size_t capacity_`
- [ ] `tests/common/blocking_queue_test.cpp` — RED tests:
  - `push` then `pop` returns same item
  - `pop` on empty queue blocks until `push` called from another thread
  - `push` on full queue blocks until `pop` called from another thread
  - `close` unblocks blocked `pop`, which returns `std::nullopt`
  - `close` unblocks blocked `push` (returns without pushing)
  - Items pushed before `close` are still retrievable via `pop`
  - Multiple producers and consumers: all items delivered exactly once (concurrent stress test)
- [ ] Update `tests/CMakeLists.txt` to include `tests/common/blocking_queue_test.cpp`

**Verify:** `cmake --build build && ./build/tests` — all tests GREEN including BlockingQueue.

---

## Phase 5: Server Infrastructure

**Integration tested via the client binary (Phase 6). No unit tests for these components.**

### 5a: SignalHandler

- [ ] `server/include/signal_handler.hpp` — class declaration: constructor takes `std::atomic<bool>& stop_flag`; installs `SIGINT` and `SIGTERM` handlers via `sigaction`
- [ ] `server/src/signal_handler.cpp` — implementation: sets `stop_flag` to `true` on signal; uses a file-scope pointer to the flag (POSIX signal handler constraint)
- [ ] Update `server/CMakeLists.txt` to include `signal_handler.cpp`

### 5b: InMemoryDroneRepository

- [ ] `server/include/in_memory_drone_repo.hpp` — class declaration: implements `IDroneRepository`; protected by `std::mutex` for thread safety
- [ ] `server/src/in_memory_drone_repo.cpp` — implementation: `std::unordered_map<std::string, Drone>`; `findById` returns copy wrapped in optional; `save` upserts by id
- [ ] Update `server/CMakeLists.txt` to include `in_memory_drone_repo.cpp`

### 5c: ConsoleAlertNotifier

- [ ] `server/include/console_alert_notifier.hpp` — class declaration: implements `IAlertNotifier`
- [ ] `server/src/console_alert_notifier.cpp` — implementation: formats and logs each `AlertTransition` via `spdlog::warn`; one log line per transition with drone_id, alert type, entered/cleared
- [ ] Update `server/CMakeLists.txt` to include `console_alert_notifier.cpp`

### 5d: TcpServer

- [ ] `server/include/tcp_server.hpp` — class declaration:
  - Constructor: `TcpServer(uint16_t port, BlockingQueue<std::vector<uint8_t>>& queue, std::atomic<bool>& stop_flag)`
  - `void run()` — blocking accept loop; called from recv thread
  - Internal: `int server_fd_`
  - `poll()` with timeout on `server_fd_` for responsive shutdown check
- [ ] `server/src/tcp_server.cpp` — POSIX implementation:
  - `socket()` → `setsockopt(SO_REUSEADDR)` → `bind()` → `listen()`
  - Sequential accept loop (one client at a time)
  - On accept: `recv()` loop into `std::vector<uint8_t>`, `q_.push(std::move(chunk))`
  - On client disconnect: close client socket, return to `accept()`
  - On `stop_flag`: close server socket, call `q_.close()`, return
  - Log connections/disconnections via spdlog
- [ ] Update `server/CMakeLists.txt` to include `tcp_server.cpp`

### 5e: Composition Root — main.cpp

- [ ] `server/src/main.cpp` — wires the pipeline:
  ```
  AlertPolicy policy{};
  InMemoryDroneRepository repo{};
  ConsoleAlertNotifier notifier{};
  ProcessTelemetry use_case{repo, notifier, policy};

  BlockingQueue<std::vector<uint8_t>> q1{256};
  BlockingQueue<Telemetry> q2{256};

  std::atomic<bool> stop_flag{false};
  SignalHandler signal_handler{stop_flag};

  TcpServer server{port, q1, stop_flag};

  StreamParser parser{[&q2](Telemetry t) { q2.push(std::move(t)); }};

  std::thread recv_thread{[&server]() { server.run(); }};
  std::thread parse_thread{[&q1, &parser, &q2]() {
      while (auto chunk = q1.pop()) parser.feed(*chunk);
      q2.close();
  }};
  std::thread process_thread{[&q2, &use_case]() {
      while (auto t = q2.pop()) use_case.execute(*t);
  }};

  process_thread.join();
  parse_thread.join();
  recv_thread.join();

  spdlog::info("Shutdown complete. CRC failures: {}, Malformed: {}",
               parser.getCrcFailCount(), parser.getMalformedCount());
  ```
- [ ] Parse `--port` CLI argument (default: `9000`); log startup with port and policy thresholds
- [ ] Update `server/CMakeLists.txt` to include `main.cpp`

**Verify:** `cmake --build build && ./build/drone_server --port 9000` starts, logs ready, and shuts down cleanly on Ctrl-C.

---

## Phase 6: Client Binary

**Exercises all 7 scenarios. Integration-tests the full server stack.**

### 6a: PacketBuilder

- [ ] `client/include/packet_builder.hpp` — class declaration: static methods returning `std::vector<uint8_t>`:
  - `static std::vector<uint8_t> validPacket(const Telemetry&)` — uses `PacketSerializer`
  - `static std::vector<uint8_t> corruptCrc(const Telemetry&)` — valid packet with last 2 bytes XORed
  - `static std::vector<uint8_t> garbageBytes(size_t count)` — random bytes that are not a valid header sequence
  - `static std::vector<uint8_t> oversizeLength()` — packet with LENGTH field set to 5000 (> MAX_PAYLOAD)
  - `static std::vector<uint8_t> fragment(std::vector<uint8_t> packet, size_t chunk_size)` — splits into chunks (for fragmented scenario, caller sends each chunk separately)
- [ ] `client/src/packet_builder.cpp` — implementation using `PacketSerializer` and `crc16`
- [ ] Update `client/CMakeLists.txt` to include `packet_builder.cpp`

### 6b: Client main.cpp — 7 Scenarios

- [ ] `client/src/main.cpp` — CLI parsing: `--scenario <name>`, `--host <addr>` (default: `127.0.0.1`), `--port <port>` (default: `9000`)
- [ ] TCP connect helper: `socket()` → `connect()` → returns `int fd`
- [ ] `normal` scenario: 1000 valid packets, 5 drone IDs cycling, 1ms delay between sends
- [ ] `fragmented` scenario: same 1000 packets, each split into 1-3 byte sends with `send()` calls; exercises parser reassembly across `recv()` calls
- [ ] `corrupt` scenario: 30% garbage bytes (random, no valid header), 20% bad-CRC packets, 50% valid; interleaved
- [ ] `stress` scenario: valid packets at maximum rate for 10 seconds; log send rate at end; target ≥ 1000 pkt/s
- [ ] `alert` scenario: send packets for 3 drones with `altitude=150.0` (above 120m limit) and `speed=60.0` (above 50 m/s limit); verify server logs alerts
- [ ] `multi-drone` scenario: 100+ unique drone IDs, 10 packets each; all drone IDs tracked by server
- [ ] `interleaved` scenario: 5 drones, packets sent round-robin in a single stream; per-drone state must remain correct
- [ ] Update `client/CMakeLists.txt` to include `main.cpp`

**Verify each scenario:**
- `normal` — server logs 1000 packets processed, 5 active drones, no CRC failures
- `fragmented` — same output as normal (parser reassembly works)
- `corrupt` — server logs CRC failures and malformed counts; no crash
- `stress` — server processes ≥ 1000 pkt/s without falling behind
- `alert` — server logs ALTITUDE and SPEED alerts for all 3 drones
- `multi-drone` — server logs 100+ active drones at shutdown
- `interleaved` — no state cross-contamination between drones

---

## Notes

**Shared libraries:** `domain` and `protocol` are linked by both `drone_client` and `drone_server`. They contain no OS-level or threading dependencies, making them safe for both binaries.

**`common` (BlockingQueue) is server-only.** The client sends packets and disconnects — it has no need for bounded queues or multi-stage pipelines.

**TDD coverage:** Domain (Phase 2) and Protocol (Phase 3) are fully unit-tested in isolation before any infrastructure exists. Common (Phase 4) is also TDD'd. Infra is integration-tested via the client binary.

**Phase ordering is a hard constraint.** Each phase depends only on phases above it:
- Phase 1 has no code deps (just CMake)
- Phase 2 has no library deps (pure C++, zero includes from deps)
- Phase 3 depends on Phase 2 (uses `Telemetry` from domain)
- Phase 4 depends on nothing (template utility, no business types)
- Phase 5 depends on Phases 2, 3, 4 (composition root links all)
- Phase 6 depends on Phase 3 (uses `PacketSerializer`, `crc16`)

**Within each phase, item ordering is a dependency constraint.** Specifically:
- Phase 2: value objects → port interfaces → fakes → Drone entity → ProcessTelemetry use case
- Phase 3: CRC16 → PacketSerializer (needs CRC16) → StreamParser (hardest; needs both)
- Phase 5: SignalHandler → Repo/Notifier → TcpServer → main.cpp (composition root last)

**Callback noexcept contract:** `StreamParser::feed()` is `noexcept`. The callback passed to its constructor must not throw. If it does, `std::terminate()` is called. This must be documented in `stream_parser.hpp`. The `q2.push()` call in the composition root satisfies this constraint — `BlockingQueue::push` does not throw (it blocks or returns on close).

**Shutdown summary logging:** At `drone_server` shutdown, log: total packets processed (tracked in process thread), CRC failure count (`parser.getCrcFailCount()`), malformed count (`parser.getMalformedCount()`), active drone count (`repo.size()` — add this getter to `InMemoryDroneRepository`).
