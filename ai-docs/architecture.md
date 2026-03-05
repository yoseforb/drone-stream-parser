# Drone Stream Parser — Architecture

**Date:** 2026-03-05
**Status:** DRAFT — under discussion
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

## 5. Decisions Made

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
| 14 | CRC16 | Internal to protocol boundary | Detail of parser/serializer, not architectural boundary |
| 15 | Concurrency | 3-stage pipeline | Conceptually I/O vs processing; pipeline justified by spec + extensibility |
| 16 | Queues | Bounded, by value with std::move | Back-pressure for embedded domain; move-efficient types |
| 17 | Queue ownership | Composition root owns, stages borrow via reference | Constructor injection, RAII lifetime |
| 18 | BlockingQueue placement | Common utility (header-only, like Go's chan) | Concurrency primitive, belongs to no boundary |
| 19 | Telemetry placement | Domain boundary | Innermost layer, Protocol depends on it (correct direction) |
| 20 | Shutdown | Cascade through pipeline | Q1.close() → Q2.close() → exit. No data dropped |
| 21 | Error handling | Exceptions + noexcept + std::optional | Three tools: optional for absence, exceptions for failures, noexcept for pure logic |
| 22 | Port signatures | optional for lookup, void+throw for mutation | findById→optional, save/notify→void (throw on failure) |

---

## 6. Open Questions

- [ ] **CMake targets** — one per boundary + common + executables
- [ ] **State machine parser** — states, transitions, resync strategy
- [ ] **Wire format** — byte-level packet layout
- [ ] **Client scenarios** — what the test client covers
- [ ] **Project directory structure** — file tree
- [ ] **Parsing stats / CRC failure counting** — minor, decide during parser design
