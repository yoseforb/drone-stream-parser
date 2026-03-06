# Drone Stream Parser — Architecture

**Date:** 2026-03-05
**Status:** FINAL — all decisions made
**Standard:** C++20 | GCC 15.2.1 | CMake 4.2.3 | Linux

---

## 1. Pragmatic Clean Architecture — 3 Boundaries

Full 4-layer Clean Architecture is over-engineering for this project's domain size.
Instead: apply Clean Architecture where it earns its keep (domain isolation),
proper component isolation where it matters (parser), and don't force
abstraction where integration testing is the right strategy (infrastructure).

```
┌──────────────────────────────────────────────────────────────┐
│                    Composition Root                            │
│    server main.cpp — creates objects, injects deps, wires     │
│    threads. Not a layer, not tested in isolation.             │
│    client main.cpp — separate binary, own entry point.        │
└────────────────────────┬─────────────────────────────────────┘
                         │ creates & injects
          ┌──────────────┼──────────────┐
          ▼              ▼              ▼
┌──────────────┐  ┌─────────────┐  ┌──────────────────────────┐
│Infrastructure│  │  Protocol   │  │         Domain           │
│              │  │             │  │                          │
│ TcpServer    │  │ StreamParser│  │ Entity:                  │
│ SignalHandler│  │ PacketSerial│  │  Drone (rich, identity   │
│ Threads      │  │ CRC16       │  │  by drone_id, owns       │
│              │  │             │  │  update + alert state)   │
│ POSIX, OS    │  │ bytes ↔     │  │                          │
│ level. No    │  │ Telemetry   │  │ Value Objects:           │
│ unit tests.  │  │             │  │  Telemetry, AlertType,   │
│ Integration  │  │ Isolated,   │  │  AlertTransition,        │
│ tested via   │  │ TDD'd.      │  │  AlertPolicy             │
│ client.      │  │             │  │                          │
│              │  │ Depends on: │  │ Use Case:                │
│ Depends on:  │  │ Domain      │  │  ProcessTelemetry        │
│ Protocol +   │  │ (Telemetry  │  │                          │
│ Domain       │  │  type only) │  │ Ports (interfaces):      │
│ (interfaces) │  │             │  │  IDroneRepository        │
│              │  │             │  │  IAlertNotifier           │
│              │  │             │  │                          │
│              │  │             │  │ Zero deps. Pure C++.     │
│              │  │             │  │ Fully TDD'd.             │
└──────────────┘  └─────────────┘  └──────────────────────────┘

                  ┌─────────────┐
                  │   Common    │
                  │ (utilities) │
                  │             │
                  │ BlockingQueue│
                  │ (header-only)│
                  │             │
                  │ Concurrency │
                  │ primitive.  │
                  │ No boundary │
                  │ — like Go's │
                  │ chan.        │
                  └─────────────┘

Dependency direction:
  Infrastructure → Protocol → Domain
  Common is available to all (concurrency primitives, no business logic)
```

### Why 3 boundaries, not 4 layers?

| Boundary | What it isolates | Test strategy | Value |
|----------|-----------------|---------------|-------|
| **Domain** | Drone entity, alert logic, use case | Pure unit tests (fakes for ports) | Test domain logic with zero setup |
| **Protocol** | State machine parser, serialization | Unit tests feeding raw bytes | Test the hardest, most bug-prone code in isolation |
| **Infrastructure** | TCP, threads, OS interaction | Integration tests via client binary | Mocking sockets/threads adds complexity without catching real bugs |

Each boundary catches a **real category of bugs independently**. None is ceremony.

---

## 2. Domain Model

### Entity: Drone

- **Identity:** `drone_id` (string)
- **State:** latest lat, lon, alt, speed, timestamp (flat fields, no Position VO — YAGNI)
- **Alert state:** `std::set<AlertType>` (extensible, no combinatorial explosion)
- **Behavior:** `updateFrom(Telemetry, AlertPolicy) → vector<AlertTransition>` **noexcept**
  - Updates fields from telemetry
  - Evaluates thresholds from AlertPolicy
  - Returns list of state changes (entered/cleared alerts)
  - Pure domain logic — cannot fail
- **Rich entity:** owns its update + alert state transition logic

### Value Objects

- **Telemetry** — immutable snapshot: drone_id, lat, lon, alt, speed, timestamp
  - Lives in Domain boundary (innermost). Protocol depends on it — correct dependency direction.
- **AlertType** — enum: `ALTITUDE`, `SPEED`
- **AlertTransition** — `{ AlertType type, bool entered }` (true=entered, false=cleared)
- **AlertPolicy** — threshold config (altitude limit, speed limit).
  - Global domain config, injected via composition root.
  - Has constexpr defaults (120.0m altitude, 50.0 m/s speed).
  - Design allows future override from CLI args or config file without changing domain code.

### Use Case: ProcessTelemetry

- `execute(Telemetry)` → void (may propagate port exceptions)
  1. Find or create Drone via IDroneRepository
  2. Call `drone.updateFrom(telemetry, alertPolicy)` (noexcept)
  3. Save updated Drone via IDroneRepository
  4. If transitions occurred → notify via IAlertNotifier

### Ports (driven/output interfaces, defined in Domain)

- **IDroneRepository:**
  - `findById(const string&) → std::optional<Drone>` — nullopt = new drone (not an error)
  - `save(const Drone&) → void` — throws on storage failure
- **IAlertNotifier:**
  - `notify(const string& drone_id, const vector<AlertTransition>&) → void` — throws on failure

---

## 3. Error Handling Strategy

Three complementary tools, each for its purpose:

| Tool | Used for | Example |
|------|----------|---------|
| `std::optional<T>` | Expected absence (not an error) | `findById()` returns nullopt for new drones |
| Exceptions | Infrastructure failures (exceptional) | Storage error, notification failure |
| `noexcept` | Pure logic that cannot fail | `Drone::updateFrom()`, `crc16()`, `StreamParser::feed()` |

### noexcept boundary map

| Function | Can fail? | Marking |
|----------|-----------|---------|
| `Drone::updateFrom()` | No — pure logic | `noexcept` |
| `AlertPolicy` comparisons | No — arithmetic | `noexcept` |
| `StreamParser::feed()` | No — pure parsing | `noexcept` |
| `crc16()` | No — pure computation | `noexcept` |
| `PacketSerializer::serialize()` | Yes — allocates | may throw (`std::bad_alloc`) |
| `IDroneRepository::save()` | Yes — I/O boundary | throws |
| `IAlertNotifier::notify()` | Yes — I/O boundary | throws |
| `ProcessTelemetry::execute()` | Yes — calls ports | propagates |

The noexcept/throwing boundary aligns with the architecture:
Domain logic + Protocol = noexcept. Port implementations = may throw.

---

## 4. Data Flow & Concurrency

### Pipeline (data flow, not thread assignment)

```
TCP socket → [recv bytes] → Q1 → [parse bytes→Telemetry] → Q2 → [process Telemetry]
                                                                        │
                                                              ┌─────────┼──────────┐
                                                              ▼         ▼          ▼
                                                         DroneRepo  AlertNotifier  Stats
```

### Concurrency model

- **Conceptually:** I/O (recv blocks) vs processing. Two concerns.
- **Practically:** 3-stage pipeline (spec requires 3+ threads; justified by future extensibility if any stage becomes heavier).
- Each stage runs in its own thread. Queues connect stages.

### Queues (BlockingQueue — common utility)

- **Q1:** `BlockingQueue<vector<uint8_t>>` — raw byte chunks, bounded
- **Q2:** `BlockingQueue<Telemetry>` — parsed telemetry, bounded
- **Bounded:** back-pressure prevents memory growth. Correct for embedded domain.
- **Element type:** by value with `std::move`. Both `vector` and `Telemetry` are move-efficient (O(1) pointer transfer). No `unique_ptr` wrapping needed.
- **Ownership:** composition root (`main()`) creates queues on stack. Pipeline stages receive `BlockingQueue<T>&` via constructor injection. Stages borrow, never own.

### Graceful shutdown (cascade)

```
Signal (SIGINT/SIGTERM) → atomic<bool> stop_flag
  → Recv stage: sees flag, closes socket, calls Q1.close()
  → Parse stage: Q1.pop() returns nullopt, calls Q2.close()
  → Process stage: Q2.pop() returns nullopt, exits
  → main() joins threads in pipeline order
```

No data silently dropped — each stage drains its input before closing its output.

---

## 5. Wire Format

### Frame layout

```
Offset  Size   Field      Description
------  ----   -----      -----------
0       1      HEADER[0]  0xAA (fixed)
1       1      HEADER[1]  0x55 (fixed)
2       2      LENGTH     uint16_t, little-endian, byte count of PAYLOAD
4       N      PAYLOAD    Serialized Telemetry (see below)
4+N     2      CRC        uint16_t, little-endian, CRC16-CCITT over bytes [0..4+N-1]
```

### Telemetry payload serialization

```
Offset      Size       Field       Type
------      ----       -----       ----
0           2          id_len      uint16_t LE, byte count of drone_id
2           id_len     drone_id    UTF-8 bytes, no null terminator
2+id_len    8          latitude    double, IEEE 754 LE
10+id_len   8          longitude   double, IEEE 754 LE
18+id_len   8          altitude    double, IEEE 754 LE
26+id_len   8          speed       double, IEEE 754 LE
34+id_len   8          timestamp   uint64_t LE, Unix epoch milliseconds
```

Total payload size: `42 + id_len` bytes.

- **Endianness:** little-endian (matches x86-64 target, no conversion needed)
- **String prefix:** uint16_t (consistent with LENGTH field size)
- **CRC variant:** CRC-16/CCITT (poly 0x1021, init 0x0000). Table-driven implementation.

---

## 6. State Machine Parser

### States and transitions

```
HUNT_HEADER → READ_LENGTH → READ_PAYLOAD → READ_CRC → HUNT_HEADER
     ↑              │                            │
     │              │ length > MAX_PAYLOAD        │ CRC mismatch
     └──────────────┴────────────────────────────┘
                  resync: rewind to header_start + 1
```

1. **HUNT_HEADER** — scan byte-by-byte for 0xAA then 0x55
2. **READ_LENGTH** — read 2 bytes → uint16_t. If > MAX_PAYLOAD (4096): resync
3. **READ_PAYLOAD** — buffer `length` bytes
4. **READ_CRC** — read 2 bytes, compute CRC over [header + length + payload]
   - Match → deserialize Telemetry, emit via callback → HUNT_HEADER
   - Mismatch → increment crc_fail_count, log, resync → HUNT_HEADER

**Resync strategy:** On CRC failure or invalid length, rewind buffer read position to one byte after the 0xAA that started this packet attempt. The "header" we found may have been random data. Re-enter HUNT_HEADER to find the next real sync point. O(n), minimal data loss.

**MAX_PAYLOAD guard (4096):** Prevents a malformed length field from causing unbounded memory allocation.

**Parsing stats:** The parser tracks `crc_fail_count` and `malformed_count` internally. Accessible via getter methods. Logged by the composition root at shutdown. Not a domain concern.

---

## 7. Client Scenarios

7 test scenarios covering all parser and domain paths:

| Scenario | What it sends | What it tests |
|----------|--------------|---------------|
| `normal` | 1000 valid packets, 5 drone IDs | Happy path, basic packet processing |
| `fragmented` | Same packets split into 1-3 byte TCP sends | Parser reassembly across recv() calls |
| `corrupt` | 30% garbage + 20% bad CRC + 50% valid | Resync, CRC failure handling, no crash |
| `stress` | Max-rate valid packets for 10 seconds | Throughput ≥ 1000 pkt/s requirement |
| `alert` | Packets with altitude=150, speed=60 | Alert threshold detection and notification |
| `multi-drone` | 100+ unique drone IDs | Drone table scaling, all tracked correctly |
| `interleaved` | Multiple drones interleaved in stream | Correct per-drone state updates when mixed |

---

## 8. CMake Targets

| Target | Type | Links | Contents |
|--------|------|-------|----------|
| `domain` | STATIC lib | — | Entities, VOs, use case, port interfaces |
| `protocol` | STATIC lib | domain | Parser, serializer, CRC16 |
| `common` | INTERFACE lib | — | BlockingQueue (header-only) |
| `drone_server` | Executable | protocol, domain, common, Threads | Infra + composition root |
| `drone_client` | Executable | protocol, domain | Test client tool |
| `tests` | Executable | protocol, domain, GTest | Unit tests |

---

## 9. Project Directory Structure

```
drone-stream-parser/
├── CMakeLists.txt                    # Root: project(), add_subdirectory()
├── common/
│   └── include/
│       └── blocking_queue.hpp        # Thread-safe bounded queue (header-only)
├── domain/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── telemetry.hpp             # Telemetry value object
│   │   ├── drone.hpp                 # Drone entity
│   │   ├── alert_types.hpp           # AlertType enum, AlertTransition
│   │   ├── alert_policy.hpp          # AlertPolicy value object
│   │   ├── i_drone_repository.hpp    # Port interface
│   │   ├── i_alert_notifier.hpp      # Port interface
│   │   └── process_telemetry.hpp     # Use case
│   └── src/
│       ├── drone.cpp
│       └── process_telemetry.cpp
├── protocol/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── stream_parser.hpp         # State machine parser
│   │   ├── packet_serializer.hpp     # Telemetry → wire bytes
│   │   └── crc16.hpp                 # CRC-16/CCITT
│   └── src/
│       ├── stream_parser.cpp
│       ├── packet_serializer.cpp
│       └── crc16.cpp
├── server/
│   ├── CMakeLists.txt
│   ├── include/
│   │   ├── tcp_server.hpp            # POSIX TCP listener
│   │   ├── signal_handler.hpp        # SIGINT/SIGTERM handling
│   │   ├── in_memory_drone_repo.hpp  # IDroneRepository impl
│   │   └── console_alert_notifier.hpp # IAlertNotifier impl
│   └── src/
│       ├── main.cpp                  # Composition root
│       ├── tcp_server.cpp
│       ├── signal_handler.cpp
│       ├── in_memory_drone_repo.cpp
│       └── console_alert_notifier.cpp
├── client/
│   ├── CMakeLists.txt
│   ├── include/
│   │   └── packet_builder.hpp        # Builds valid/corrupt/garbage packets
│   └── src/
│       ├── main.cpp                  # CLI: --scenario, --host, --port
│       └── packet_builder.cpp
└── tests/
    ├── CMakeLists.txt
    ├── domain/
    │   ├── drone_test.cpp
    │   ├── process_telemetry_test.cpp
    │   └── fakes/
    │       ├── fake_drone_repository.hpp
    │       └── fake_alert_notifier.hpp
    └── protocol/
        ├── parser_test.cpp
        ├── serializer_test.cpp
        └── crc16_test.cpp
```

---

## 10. Decisions Made

| # | Topic | Decision | Rationale |
|---|-------|----------|-----------|
| 1 | C++ standard | C++20 | std::span, modern features |
| 2 | Test framework | GTest + GMock via FetchContent | Fakes for internal interfaces, mocks for external |
| 3 | Architecture | 3 pragmatic boundaries | Clean Arch where it earns its keep, not forced everywhere |
| 4 | Dependency direction | Inward only | Infrastructure → Protocol → Domain |
| 5 | Position value object | No — keep flat | YAGNI, no position-specific behavior |
| 6 | Drone entity | Rich entity | Owns updateFrom() + alert state logic |
| 7 | Alert model | Hybrid | Drone tracks state, use case decides when to notify (transitions) |
| 8 | Alert state | std::set&lt;AlertType&gt; | Extensible, no combinatorial explosion |
| 9 | Update result | vector&lt;AlertTransition&gt; | Drone reports what changed, use case acts on it |
| 10 | Alert thresholds | AlertPolicy (constexpr defaults, injectable) | Composition root constructs; future CLI/config override without domain changes |
| 11 | Driving port (input) | No interface | Use case receives Telemetry as plain data via execute() |
| 12 | Driven ports (output) | IDroneRepository, IAlertNotifier | Use case defines, adapters implement |
| 13 | Client binary | Separate binary | Independent test tool, not part of server architecture |
| 14 | CRC16 | CRC-16/CCITT, internal to protocol | Poly 0x1021, init 0x0000, table-driven |
| 15 | Concurrency | 3-stage pipeline | Conceptually I/O vs processing; pipeline justified by spec + extensibility |
| 16 | Queues | Bounded, by value with std::move | Back-pressure for embedded domain; move-efficient types |
| 17 | Queue ownership | Composition root owns, stages borrow via reference | Constructor injection, RAII lifetime |
| 18 | BlockingQueue placement | Common utility (header-only, like Go's chan) | Concurrency primitive, belongs to no boundary |
| 19 | Telemetry placement | Domain boundary | Innermost layer, Protocol depends on it (correct direction) |
| 20 | Shutdown | Cascade through pipeline | Q1.close() → Q2.close() → exit. No data dropped |
| 21 | Error handling | Exceptions + noexcept + std::optional | Three tools: optional for absence, exceptions for failures, noexcept for pure logic |
| 22 | Port signatures | optional for lookup, void+throw for mutation | findById→optional, save/notify→void (throw on failure) |
| 23 | Directory layout | Flat — one dir per CMake target | Self-contained, matches targets 1:1 |
| 24 | CMake targets | 6 targets | domain, protocol, common (INTERFACE), drone_server, drone_client, tests |
| 25 | Wire format | uint16_t string prefix, little-endian, CRC-16/CCITT | Consistent field sizes, matches target platform |
| 26 | Parser | 4-state machine, rewind resync | HUNT→LENGTH→PAYLOAD→CRC, MAX_PAYLOAD=4096 guard |
| 27 | Parser stats | Internal to parser, getters for counts | Not a domain concern, logged at shutdown |
| 28 | Client scenarios | 7 scenarios | normal, fragmented, corrupt, stress, alert, multi-drone, interleaved |
| 29 | Integration verification | Structured stdout + shutdown summary | No query API. Unit tests prove correctness; client demos + console output for examiner |
| 30 | Logging | spdlog via FetchContent (compiled) | De facto C++ standard. Leveled, structured, fast. Common utility available to all boundaries. |
