# Drone Stream Parser — Architecture

**Date:** 2026-03-04
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
│ BlockingQueue│  │ PacketSerial│  │  Drone (rich, identity   │
│ SignalHandler│  │ CRC16       │  │  by drone_id, owns       │
│ Threads      │  │             │  │  update + alert state)   │
│              │  │ bytes ↔     │  │                          │
│ POSIX, OS    │  │ Telemetry   │  │ Value Objects:           │
│ level. No    │  │             │  │  Telemetry, AlertType,   │
│ unit tests.  │  │ Isolated,   │  │  AlertTransition,        │
│ Integration  │  │ TDD'd.      │  │  AlertPolicy             │
│ tested via   │  │             │  │                          │
│ client.      │  │ Depends on: │  │ Use Case:                │
│              │  │ Domain      │  │  ProcessTelemetry        │
│ Depends on:  │  │ (Telemetry  │  │                          │
│ Protocol +   │  │  type only) │  │ Ports (interfaces):      │
│ Domain       │  │             │  │  IDroneRepository        │
│ (interfaces) │  │             │  │  IAlertNotifier           │
│              │  │             │  │                          │
│              │  │             │  │ Zero deps. Pure C++.     │
│              │  │             │  │ Fully TDD'd.             │
└──────────────┘  └─────────────┘  └──────────────────────────┘

Dependency direction:
  Infrastructure → Protocol → Domain
  (outer depends on inner, never reverse)
```

### Why 3 boundaries, not 4 layers?

| Boundary | What it isolates | Test strategy | Value |
|----------|-----------------|---------------|-------|
| **Domain** | Drone entity, alert logic, use case | Pure unit tests (fakes for ports) | Test 50 lines of domain logic with zero setup |
| **Protocol** | State machine parser, serialization | Unit tests feeding raw bytes | Test the hardest, most bug-prone code in isolation |
| **Infrastructure** | TCP, threads, OS interaction | Integration tests via client binary | Mocking sockets/threads adds complexity without catching real bugs |

Each boundary catches a **real category of bugs independently**. None is ceremony.

---

## 2. Domain Model

### Entity: Drone

- **Identity:** `drone_id` (string)
- **State:** latest lat, lon, alt, speed, timestamp (flat fields, no Position VO — YAGNI)
- **Alert state:** `std::set<AlertType>` (extensible, no combinatorial explosion)
- **Behavior:** `updateFrom(Telemetry, AlertPolicy) → vector<AlertTransition>`
  - Updates fields from telemetry
  - Evaluates thresholds from AlertPolicy
  - Returns list of state changes (entered/cleared alerts)
- **Rich entity:** owns its update + alert state transition logic

### Value Objects

- **Telemetry** — immutable snapshot: drone_id, lat, lon, alt, speed, timestamp
- **AlertType** — enum: `ALTITUDE`, `SPEED`
- **AlertTransition** — `{ AlertType type, bool entered }` (true=entered, false=cleared)
- **AlertPolicy** — threshold config (altitude limit, speed limit). Global domain config, injected.

### Use Case: ProcessTelemetry

- `execute(Telemetry)` →
  1. Find or create Drone via IDroneRepository
  2. Call `drone.updateFrom(telemetry, alertPolicy)`
  3. Save updated Drone via IDroneRepository
  4. If transitions occurred → notify via IAlertNotifier

### Ports (driven/output interfaces, defined in Domain)

- **IDroneRepository:** `findById(string) → optional<Drone>`, `save(Drone)`
- **IAlertNotifier:** `notify(string drone_id, vector<AlertTransition>)`

---

## 3. Decisions Made

| # | Topic | Decision | Rationale |
|---|-------|----------|-----------|
| 1 | C++ standard | C++20 | std::span, modern features |
| 2 | Test framework | GTest + GMock via FetchContent | Fakes for internal interfaces, mocks for external |
| 3 | Architecture | 3 pragmatic boundaries | Clean Arch where it earns its keep, not forced everywhere, full 4-layer is over-engineering for this domain size |
| 4 | Dependency direction | Inward only | Infrastructure → Protocol → Domain |
| 5 | Position value object | No — keep flat | YAGNI, no position-specific behavior |
| 6 | Drone entity | Rich entity | Owns updateFrom() + alert state logic |
| 7 | Alert model | Hybrid | Drone tracks state, use case decides when to notify (transitions) |
| 8 | Alert state | std::set&lt;AlertType&gt; | Extensible, no combinatorial explosion |
| 9 | Update result | vector&lt;AlertTransition&gt; | Drone reports what changed, use case acts on it |
| 10 | Alert thresholds | AlertPolicy (global domain config) | Injected, separates configuration from logic |
| 11 | Driving port (input) | No interface | Use case receives Telemetry as plain data via execute() |
| 12 | Driven ports (output) | IDroneRepository, IAlertNotifier | Use case defines, adapters implement |
| 13 | Client binary | Separate binary | Independent test tool, not part of server architecture |
| 14 | CRC16 | Internal to protocol boundary | Detail of parser/serializer, not architectural boundary |

---

## 4. Open Questions

- [ ] **Parsing stats / CRC failure counting** — where does it belong?
- [ ] **AlertPolicy injection** — how does it flow from config to Drone entity?
- [ ] **Use case port signatures** — full method signatures, error handling
- [ ] **Threading model** — how threads map to boundaries
- [ ] **CMake targets** — one per boundary + executables
- [ ] **State machine parser** — states, transitions, resync strategy
- [ ] **Wire format** — byte-level packet layout
- [ ] **Graceful shutdown** — signal propagation
- [ ] **Client scenarios** — what the test client covers
- [ ] **Project directory structure** — file tree
