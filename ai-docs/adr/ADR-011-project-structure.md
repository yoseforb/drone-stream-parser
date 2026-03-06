# ADR-011: Project Structure

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #1 (C++20), #23 (flat layout), #24 (6 CMake targets), #28 (7 client scenarios)

## Context

Needed a project structure that enforces the architectural boundaries at build time, supports TDD workflow, and is straightforward to navigate. Also needed to choose the C++ standard and define what the client binary covers for integration testing.

## Decision

**C++ standard:** C++20. Required for `std::span`, concepts, and other modern features. Supported by GCC 15.2.1.

**Directory layout:** Flat — one directory per CMake target. Each directory is self-contained with its own `CMakeLists.txt`, `include/`, and `src/`. No nested hierarchies within boundaries.

```
drone-stream-parser/
  common/       -> INTERFACE lib (header-only BlockingQueue)
  domain/       -> STATIC lib (entities, VOs, use case, ports)
  protocol/     -> STATIC lib (parser, serializer, CRC)
  server/       -> Executable (infrastructure + composition root)
  client/       -> Executable (test tool)
  tests/        -> Executable (GTest unit tests)
```

**6 CMake targets:**

| Target | Type | Links |
|--------|------|-------|
| `domain` | STATIC lib | (none) |
| `protocol` | STATIC lib | domain |
| `common` | INTERFACE lib | (none) |
| `drone_server` | Executable | protocol, domain, common, Threads |
| `drone_client` | Executable | protocol, domain |
| `tests` | Executable | protocol, domain, GTest |

**7 client scenarios:** normal, fragmented, corrupt, stress, alert, multi-drone, interleaved. Each scenario targets a specific category of behavior: happy path, parser reassembly, error recovery, throughput, domain alerts, scaling, and concurrent state correctness.

## Alternatives Considered

- **C++17** — rejected. Lacks `std::span` and several quality-of-life features. C++20 is well-supported by the target compiler.
- **C++23** — rejected. Not fully supported by GCC 15.2.1 for all features. C++20 provides everything needed.
- **Nested directory structure (src/domain/, src/protocol/)** — rejected. Flat structure makes each target's boundary visible at the top level. A `src/` parent directory adds a nesting level with no benefit.
- **Single library target** — rejected. Separate targets enforce dependency direction at link time. If domain accidentally includes a protocol header, the build fails.
- **Fewer client scenarios** — rejected. Each scenario catches a distinct failure mode. Removing any would leave a category of bugs untested in integration.

## Consequences

- **Positive:** CMake link dependencies enforce architectural boundaries — wrong-direction includes cause build failures.
- **Positive:** Flat layout is easy to navigate. `ls` at the root shows all boundaries.
- **Positive:** 7 scenarios cover all parser states (fragmented, corrupt), domain paths (alerts, multi-drone), and non-functional requirements (stress throughput).
- **Positive:** Each target can be built and tested independently during development.
- **Negative:** 6 targets for a small project may feel heavy. However, each target serves a purpose (boundary enforcement or separate binary), and none is empty ceremony.
