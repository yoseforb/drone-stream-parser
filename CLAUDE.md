# Drone Stream Parser

Multi-threaded C++20 streaming parser for drone telemetry over TCP.

---

## Build Commands (cmake-init presets)

```bash
# Configure + Build + Test (dev preset — Debug, strict warnings, clang-tidy, -Werror)
cmake --preset=dev
cmake --build --preset=dev
ctest --preset=dev

# Format
cmake --build --preset=dev -t format-check   # check only
cmake --build --preset=dev -t format-fix     # auto-fix

# Direct binary paths
./build/dev/server/drone_server --port 9000
./build/dev/client/drone_client --scenario normal --host 127.0.0.1 --port 9000
./build/dev/test/tests
```

## Running Server & Client

```bash
# Start server (use any free port)
./build/dev/server/drone_server --port 9000

# In another terminal, run client scenarios
./build/dev/client/drone_client --scenario <name> --host 127.0.0.1 --port 9000
```

**Available scenarios:**

| Scenario       | Description                                              |
|----------------|----------------------------------------------------------|
| `normal`       | 1000 valid packets across 5 drones                       |
| `fragmented`   | 1000 packets sent as small TCP chunks                    |
| `corrupt`      | 100 iterations: 30% garbage, 20% bad CRC, 50% valid     |
| `stress`       | Max-rate send for 10s (~460K pkt/s)                      |
| `alert`        | Packets above altitude/speed thresholds                  |
| `multi-drone`  | 100 unique drones, 10 packets each                       |
| `interleaved`  | 5 drones round-robin, 50 rounds                          |
| `all`          | Runs all 7 scenarios above in sequence                   |

Stop the server with `Ctrl+C` (SIGINT) for graceful shutdown with stats summary.

## MANDATORY: Build and Test After Every Change

**After every code change, you MUST run:**
```bash
# 1. Build + unit tests
cmake --build --preset=dev && ctest --preset=dev

# 2. Integration test: start server, run all client scenarios, stop server
./build/dev/server/drone_server --port 9100 &
sleep 1
./build/dev/client/drone_client --scenario all --host 127.0.0.1 --port 9100
kill -INT %1
```

**Rules — no exceptions:**
1. Run build + unit tests + integration test after EVERY code change, no matter how small.
2. NEVER `git commit` before build, unit tests, AND integration test pass. A commit without a green build is forbidden.
3. Do not consider a change complete until the build passes with zero warnings, all unit tests pass, and the server handles all client scenarios cleanly.
4. clang-tidy runs as part of the build — treat its diagnostics as errors.
5. If build or tests fail, fix all issues and re-run before doing anything else.

---

## Architecture Documentation

- **Architecture:** `ai-docs/architecture.md`
- **Master plan:** `ai-docs/planning/active/master-plan.md`

### ADRs (`ai-docs/adr/`)

- ADR-001 — Pragmatic Clean Architecture
- ADR-002 — Domain Model Design
- ADR-003 — Alert Threshold Configuration
- ADR-004 — Port and Interface Design
- ADR-005 — Error Handling Strategy
- ADR-006 — Concurrency Model and Data Flow
- ADR-007 — Blocking Queue as Common Utility
- ADR-008 — Wire Format and CRC
- ADR-009 — State Machine Parser Design
- ADR-010 — Test Strategy and Framework
- ADR-011 — Project Structure
- ADR-012 — Logging with spdlog

### C4 Docs (`ai-docs/c4/`)

- `context.md`
- `container.md`
- `component.md`
- `code.md`

---

## Code Style

- **Standard:** C++20, GCC 15.2.1, Linux target
- **`.clang-tidy`** enforces strict checks with warnings-as-errors
- **`.clang-format`** uses LLVM base style with left pointer alignment

### Naming Conventions

| Element                              | Convention                              |
|--------------------------------------|-----------------------------------------|
| Classes/Structs                      | CamelCase                               |
| Functions/Methods                    | camelBack                               |
| Variables/Parameters                 | snake_case                              |
| Member variables                     | snake_case with trailing `_` (`buffer_`)|
| Public members (structs/VOs)         | snake_case without suffix               |
| Constants/Constexpr                  | CamelCase                               |
| Enums                                | CamelCase, values: UPPER_CASE           |
| Namespaces                           | snake_case                              |

---

## Project Structure (CMake Targets)

| Target         | Type         | Links                        | Contents                                   |
|----------------|--------------|------------------------------|--------------------------------------------|
| `domain`       | STATIC lib   | --                           | Entities, VOs, use case, port interfaces   |
| `protocol`     | STATIC lib   | domain                       | Parser, serializer, CRC16                  |
| `common`       | INTERFACE lib| --                           | BlockingQueue (header-only)                |
| `drone_server` | Executable   | protocol, domain, common, Threads, spdlog | Infrastructure + composition root |
| `drone_client` | Executable   | protocol, domain, spdlog     | Test client tool                           |
| `tests`        | Executable   | protocol, domain, GTest      | Unit tests                                 |

---

## Workflow

TDD per component: **interface -> stub -> test (RED) -> implementation (GREEN) -> refactor**

- Dependency direction: Infrastructure -> Protocol -> Domain
- Domain and Protocol are fully TDD'd with unit tests
- Infrastructure is integration-tested via the client binary

---

## Key Constraints

- NEVER signature commit message with Claude
- No backward compatibility concerns (greenfield project)
- No over-engineering -- build what is needed now
- **noexcept boundary:** Domain logic + Protocol = noexcept. Port implementations = may throw.
