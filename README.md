# Drone Stream Parser

A multi-threaded C++20 application that parses continuous binary telemetry streams from drones over TCP. The system handles fragmented packets, corrupted data, and loss of synchronization -- as expected in real embedded/RF communication layers. Built for Linux with POSIX sockets.

## Architecture

The project uses a pragmatic Clean Architecture with three boundaries, each isolating a real category of bugs with its own test strategy:

| Boundary | Responsibility | Test Strategy |
|---|---|---|
| **Domain** | Drone entity, alert logic, use case, port interfaces | Unit tests with fakes |
| **Protocol** | State machine parser, serialization, CRC16 | Unit tests with raw bytes |
| **Infrastructure** | TCP server, threads, signal handling, port implementations | Integration tests via client binary |

Dependency direction is strictly inward: **Infrastructure -> Protocol -> Domain**. CMake targets enforce this at build time -- architectural violations are compile errors.

### 3-Thread Pipeline

Data flows through a 3-stage pipeline connected by bounded `BlockingQueue`s:

```
TCP recv -> Q1 [raw bytes] -> parse -> Q2 [Telemetry] -> process
 Thread 1                     Thread 2                    Thread 3
```

- **Q1:** `BlockingQueue<vector<uint8_t>>` -- raw byte chunks from `recv()`
- **Q2:** `BlockingQueue<Telemetry>` -- parsed telemetry objects

Queues are bounded with blocking back-pressure. When downstream is slow, upstream blocks on push (sleeping on a condition variable, zero CPU). The pipeline self-regulates to match the slowest stage. No data is silently dropped.

### State Machine Parser

The `StreamParser` implements a 5-state machine for stream protocol parsing:

```
HUNT_HEADER -> READ_LENGTH -> READ_PAYLOAD -> READ_CRC -> COMPLETE_FRAME -> HUNT_HEADER
     ^              |                              |              |
     |              | length > MAX_PAYLOAD         | CRC mismatch | deserialization failure
     +--------------+------------------------------+--------------+
                          resync: rewind to header_start + 1
```

`READ_CRC` validates the CRC16 checksum. On success, `COMPLETE_FRAME` deserializes the payload into a `Telemetry` struct, compacts the buffer, resets parser state, and invokes the callback. On CRC failure, invalid length, or deserialization failure, the parser rewinds to one byte after the `0xAA` that started the failed packet attempt and re-enters `HUNT_HEADER`. This handles the case where an apparent header (`0xAA55`) was actually random data mid-stream. A `MAX_PAYLOAD` guard of 4096 bytes prevents malformed length fields from causing unbounded allocation.

### Wire Format

```
Offset  Size   Field      Description
------  ----   -----      -----------
0       1      HEADER[0]  0xAA (fixed)
1       1      HEADER[1]  0x55 (fixed)
2       2      LENGTH     uint16_t, little-endian, byte count of PAYLOAD
4       N      PAYLOAD    Serialized Telemetry
4+N     2      CRC        uint16_t, little-endian, CRC16-CCITT over bytes [0..4+N-1]
```

### Graceful Shutdown

Shutdown cascades through the pipeline on `SIGINT`/`SIGTERM`:

1. Recv stage sees the stop flag, closes the listening socket, calls `Q1.close()`
2. Parse stage receives `nullopt` from `Q1.pop()`, calls `Q2.close()`
3. Process stage receives `nullopt` from `Q2.pop()`, exits
4. `main()` joins threads in pipeline order

Each stage drains its input before closing its output. No data is lost.

For deeper architectural details, see `ai-docs/architecture.md`, the ADRs in `ai-docs/adr/`, and the C4 model in `ai-docs/c4/`.

## Building and Running

### Prerequisites

- GCC 15+ (or Clang with C++20 support)
- CMake 3.14+
- Linux (POSIX sockets)

Dependencies (fetched automatically via CMake `FetchContent`):
- [GoogleTest](https://github.com/google/googletest) -- unit testing
- [spdlog](https://github.com/gabime/spdlog) -- logging

### Build

```bash
cmake --preset=dev
cmake --build --preset=dev
ctest --preset=dev
```

The `dev` preset enables Debug mode, strict warnings (`-Werror`), and clang-tidy static analysis. All clang-tidy diagnostics are treated as errors.

### Running

Start the server on any free port:

```bash
./build/dev/server/drone_server --port 9000
```

In a separate terminal, run a client scenario:

```bash
./build/dev/client/drone_client --scenario <name> --host 127.0.0.1 --port 9000
```

Stop the server with `Ctrl+C` for graceful shutdown with a stats summary.

| Scenario | Description |
|---|---|
| `normal` | 1000 valid packets across 5 drones |
| `fragmented` | 1000 packets sent as small TCP chunks |
| `corrupt` | 100 iterations: 30% garbage, 20% bad CRC, 50% valid |
| `stress` | Max-rate send for 10 seconds (~460K pkt/s) |
| `alert` | Packets above altitude/speed thresholds |
| `multi-drone` | 100 unique drones, 10 packets each |
| `interleaved` | 5 drones round-robin, 50 rounds |
| `all` | Runs all 7 scenarios above in sequence |

## Testing

### Unit Tests

69 unit tests via GoogleTest, spread across 7 test files:

| Test File | Covers |
|---|---|
| `tests/domain/drone_test.cpp` | Drone entity: creation, updates, alert state transitions |
| `tests/domain/process_telemetry_test.cpp` | ProcessTelemetry use case: orchestration, port interactions |
| `tests/protocol/parser_test.cpp` | StreamParser: fragmentation, corruption, resync, edge cases |
| `tests/protocol/serializer_test.cpp` | PacketSerializer: round-trip serialization/deserialization |
| `tests/protocol/crc16_test.cpp` | CRC16-CCITT: known vectors, edge cases |
| `tests/common/blocking_queue_test.cpp` | BlockingQueue: thread safety, bounding, close semantics |
| `tests/client/packet_builder_test.cpp` | PacketBuilder: valid, corrupt, and fragmented packet generation |

Domain tests use test fakes (`FakeDroneRepository`, `FakeAlertNotifier`) to isolate domain logic from infrastructure.

Run unit tests:

```bash
ctest --preset=dev
```

### Integration Tests

The client binary serves as the integration test suite, exercising the full system end-to-end:

```bash
./build/dev/server/drone_server --port 9100 &
sleep 1
./build/dev/client/drone_client --scenario all --host 127.0.0.1 --port 9100
kill -INT %1
```

### Static Analysis

clang-tidy runs as part of the `dev` build with warnings-as-errors. No separate invocation needed.

## Development Workflow

This section describes the methodology used to develop the project: **SDD -> IDD -> TDD** (Spec-Driven Design -> Interface-Driven Design -> Test-Driven Development), orchestrated through Claude Code as an AI pair-programming assistant.

### Spec-Driven Design (SDD)

Before writing any code, the entire system was designed upfront:

- Full architecture document covering all major design decisions with rationale
- 12 Architecture Decision Records (ADRs), each evaluating alternatives and documenting trade-offs
- C4 model documentation at all 4 levels (Context, Container, Component, Code)
- Detailed phase-by-phase implementation plans serving as executable specifications

All design artifacts are preserved in `ai-docs/`.

### Interface-Driven Design (IDD)

Each implementation phase started by defining interfaces before any logic:

- Port interfaces (`IDroneRepository`, `IAlertNotifier`) defined in the Domain boundary
- Test fakes written against interfaces before real implementations
- Dependency inversion enforced structurally via CMake targets -- architectural violations are compile errors

### Test-Driven Development (TDD)

Every component followed RED -> GREEN -> REFACTOR:

- **RED**: Write tests against stubs/interfaces that fail
- **GREEN**: Implement minimum code to pass tests
- **REFACTOR**: Clean up while keeping tests green

Every code change triggered a mandatory feedback loop: build with strict clang-tidy (warnings-as-errors, nearly all checks enabled), run all unit tests, and run integration tests. This created a tight quality gate -- clang-tidy enforces naming conventions, modern C++ idioms, and potential bug patterns, while the test suite catches logic errors. Claude Code had to fix all diagnostics before moving forward, which drove higher code quality than a permissive build would.

### Claude Code as Development Partner

The project used Claude Code (Anthropic's CLI coding assistant) throughout:

- `CLAUDE.md` provided project context, build commands, and mandatory rules (e.g., build+test after every change)
- Planning documents in `ai-docs/planning/` served as detailed step-by-step instructions
- All documentation in `ai-docs/` was AI-generated and human-reviewed

## Project Structure

### CMake Targets

| Target | Type | Links | Contents |
|---|---|---|---|
| `domain` | STATIC lib | -- | Entities, VOs, use case, port interfaces |
| `protocol` | STATIC lib | domain | Parser, serializer, CRC16 |
| `common` | INTERFACE lib | -- | BlockingQueue (header-only) |
| `drone_server` | Executable | protocol, domain, common, Threads, spdlog | Infrastructure + composition root |
| `drone_client` | Executable | protocol, domain, spdlog | Test client tool |
| `tests` | Executable | protocol, domain, GTest | Unit tests |

### Directory Layout

```
drone-stream-parser/
├── common/          BlockingQueue (header-only concurrency primitive)
├── domain/          Entities, value objects, use case, port interfaces
├── protocol/        State machine parser, serializer, CRC16
├── server/          TCP server, signal handling, composition root
├── client/          Test client with 7 scenarios
├── tests/           Unit tests (domain/, protocol/, common/, client/)
└── ai-docs/         Architecture docs, ADRs, C4 model, planning
```

## Production Improvements

The following changes would be needed for a production deployment:

1. **Configuration file** -- AlertPolicy thresholds (altitude limit, speed limit) are currently compile-time constants. Production needs a config file (YAML/TOML) to load thresholds, queue capacities, and other tuning parameters at startup.

2. **Environment variable support** -- Server port and bind address should be configurable via environment variables (e.g., `DRONE_SERVER_PORT`, `DRONE_SERVER_HOST`) in addition to CLI flags, following the 12-factor app methodology.

3. **Multi-client support** -- The current TcpServer handles one client connection at a time (sequential accept). Production requires concurrent client handling, either via a thread-per-connection model or an event-driven approach (epoll).

4. **Automated integration testing** -- Integration tests currently require manually starting the server, running the client, and stopping the server. A proper CI pipeline would automate this with a test harness that manages server lifecycle, runs all scenarios, and validates output programmatically.

5. **TLS/SSL encryption** -- TCP connections are currently plaintext. Production deployments need TLS for data confidentiality and authentication.

6. **Log management** -- spdlog outputs to console only. Production needs file-based logging with rotation, configurable log levels, and potentially structured JSON output for log aggregation systems.

7. **Metrics and monitoring** -- Expose runtime statistics (parse rate, CRC failure rate, active drone count, queue depths) via a metrics endpoint for monitoring dashboards and alerting.

8. **Drone timeout and eviction** -- Drones that stop reporting are never removed from the in-memory map. Production needs TTL-based eviction to prevent unbounded memory growth.

9. **Persistent storage** -- `InMemoryDroneRepository` loses all state on restart. Production may need database-backed storage for historical data and crash recovery.

## Documentation References

All design documentation is in `ai-docs/`:

- **Architecture:** `ai-docs/architecture.md`
- **C4 Model:** `ai-docs/c4/` -- context, container, component, code level diagrams
- **Planning:** `ai-docs/planning/` -- phase-by-phase implementation plans

### Architecture Decision Records (`ai-docs/adr/`)

| ADR | Title |
|---|---|
| ADR-001 | Pragmatic Clean Architecture |
| ADR-002 | Domain Model Design |
| ADR-003 | Alert Threshold Configuration |
| ADR-004 | Port and Interface Design |
| ADR-005 | Error Handling Strategy |
| ADR-006 | Concurrency Model and Data Flow |
| ADR-007 | Blocking Queue as Common Utility |
| ADR-008 | Wire Format and CRC |
| ADR-009 | State Machine Parser Design |
| ADR-010 | Test Strategy and Framework |
| ADR-011 | Project Structure |
| ADR-012 | Logging with spdlog |
