# ADR-007: BlockingQueue as Common Utility

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #18 (queue placement outside boundaries), #19 (Telemetry stays in domain)

## Context

BlockingQueue is used by multiple boundaries — Infrastructure creates and owns queues, Protocol's parsing stage reads from one and writes to another, Domain's Telemetry flows through them. The question was where BlockingQueue belongs. It cannot live in any single boundary without creating a wrong-direction dependency. A related question was whether Telemetry should move to Common since it flows through queues — but that would hollow out the domain.

## Decision

**BlockingQueue placement:** Common utility area, as a header-only library. It is a concurrency primitive analogous to Go's `chan` — a language-level building block that belongs to no business boundary. The Common area has no business logic and is available to all boundaries.

**Telemetry placement:** Stays in Domain (innermost boundary). Protocol depends on Domain's Telemetry type, which is the correct dependency direction. Moving Telemetry to Common would weaken the domain model for the sake of a packaging convenience.

## Alternatives Considered

- **BlockingQueue in Infrastructure** — rejected. Protocol's parse stage needs it, creating an Infrastructure <- Protocol dependency (wrong direction).
- **BlockingQueue in Domain** — rejected. It is a concurrency primitive with no domain semantics. Pollutes the domain with threading concerns.
- **BlockingQueue in Protocol** — rejected. Same wrong-dependency problem as Infrastructure, and it has nothing to do with protocol parsing.
- **Telemetry in Common** — rejected. Telemetry is the core domain value object. Moving it to a "utility" area to avoid a dependency would be the tail wagging the dog. Protocol depending on Domain is correct and intentional.

## Consequences

- **Positive:** BlockingQueue is available to all boundaries without creating dependency cycles.
- **Positive:** Telemetry stays where it semantically belongs (Domain), and the dependency graph remains clean: Infrastructure -> Protocol -> Domain, with Common available to all.
- **Positive:** Header-only implementation means Common is an INTERFACE library in CMake — no compiled artifact, just an include path.
- **Negative:** Common is a grab-bag area. If more utilities accumulate, it could become a dumping ground. Currently it contains only BlockingQueue, so this is not a concern.
