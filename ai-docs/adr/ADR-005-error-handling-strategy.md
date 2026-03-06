# ADR-005: Error Handling Strategy

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #21 (exceptions + noexcept + optional), #22 (port signatures)

## Context

Coming from a Go/Rust background where error handling is value-based (Result<T,E>, error returns), the question was which C++ error strategy to use. Discussed Result<T,E> wrappers, exceptions-everywhere, and noexcept boundaries. Also considered what a C++ examiner would expect — idiomatic C++ uses exceptions for exceptional failures, not for control flow.

## Decision

Three complementary tools, each for its specific purpose:

| Tool | Used for | Example |
|------|----------|---------|
| `std::optional<T>` | Expected absence (not an error) | `findById()` returns nullopt for new drones |
| Exceptions | Infrastructure failures (exceptional) | Storage error, notification failure |
| `noexcept` | Pure logic that cannot fail | `Drone::updateFrom()`, `crc16()`, `StreamParser::feed()` |

**Port signatures follow this pattern:**
- `IDroneRepository::findById()` -> `std::optional<Drone>` (nullopt = new drone, not an error)
- `IDroneRepository::save()` -> `void` (throws on storage failure)
- `IAlertNotifier::notify()` -> `void` (throws on failure)
- `ProcessTelemetry::execute()` -> `void` (propagates port exceptions)

**The noexcept/throwing boundary aligns with the architecture:** Domain logic + Protocol = noexcept. Port implementations = may throw.

## Alternatives Considered

- **Result<T,E> everywhere (Rust-style)** — rejected. Not idiomatic C++. Would require a custom Result type or a library dependency. Adds boilerplate at every call site for error propagation that exceptions handle automatically.
- **Exceptions everywhere (including domain logic)** — rejected. Domain logic like threshold comparison and CRC computation cannot fail. Marking them noexcept communicates intent and enables compiler optimizations.
- **Error codes (C-style)** — rejected. Loses type safety, requires manual checking at every call site, easy to ignore silently.
- **std::expected (C++23)** — rejected. Project targets C++20. Even with C++23, the same argument as Result<T,E> applies for this project's complexity level.

## Consequences

- **Positive:** Each error handling tool matches its semantic context — no square-peg-round-hole forcing.
- **Positive:** `noexcept` on pure logic functions documents and enforces that they cannot fail, enabling reasoning about error boundaries.
- **Positive:** `std::optional` for findById avoids the "exception for expected case" anti-pattern (a new drone is normal, not exceptional).
- **Positive:** Idiomatic C++ — an examiner sees exceptions used correctly, not avoided out of cross-language habits.
- **Negative:** Exception safety requires care in port implementations. Infrastructure adapters must ensure RAII and exception-safe resource management.
