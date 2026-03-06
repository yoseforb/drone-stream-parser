# ADR-001: Pragmatic Clean Architecture with 3 Boundaries

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #3 (architecture style), #4 (dependency direction)

## Context

The project needs a clear architecture to isolate domain logic, protocol parsing, and infrastructure concerns. Full 4-layer Clean Architecture was evaluated but the domain is too small to justify Application/Interface layers as separate boundaries. The question was whether Clean Architecture fits at all — the conclusion was it earns its keep for domain isolation but forcing all four layers would be ceremony without value.

## Decision

Use 3 pragmatic boundaries instead of 4 Clean Architecture layers:

- **Domain** — entities, value objects, use case, port interfaces. Zero dependencies. Pure C++.
- **Protocol** — state machine parser, serialization, CRC. Depends only on Domain (Telemetry type).
- **Infrastructure** — TCP server, signal handling, threads, port implementations. Depends on Protocol and Domain.

A **Composition Root** (not a layer) wires everything together. A **Common** utility area holds concurrency primitives available to all boundaries.

Dependency direction is strictly inward: Infrastructure -> Protocol -> Domain.

## Alternatives Considered

- **Full 4-layer Clean Architecture** — rejected. Application and Interface layers would be near-empty wrappers. The domain has one use case and two ports; splitting further adds files without adding testability or isolation.
- **No formal architecture (flat modules)** — rejected. Domain logic and protocol parsing are genuinely different concerns with different test strategies. Mixing them would make the parser untestable in isolation.
- **Hexagonal Architecture** — effectively what we chose, but with Protocol as a recognized boundary. Hex arch doesn't naturally account for a protocol layer that sits between adapters and domain.

## Consequences

- **Positive:** Each boundary catches a real category of bugs independently. Domain is unit-tested with fakes, Protocol is unit-tested with raw bytes, Infrastructure is integration-tested via the client binary.
- **Positive:** CMake targets enforce boundary separation at build time — a domain target cannot accidentally link infrastructure.
- **Positive:** Minimal ceremony — no empty adapter layers, no interface-for-the-sake-of-interface.
- **Negative:** Doesn't follow textbook Clean Architecture exactly, which may surprise readers expecting the standard 4-layer diagram.
