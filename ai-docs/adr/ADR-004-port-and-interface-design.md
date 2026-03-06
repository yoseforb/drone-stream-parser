# ADR-004: Port and Interface Design

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #11 (no driving port interface), #12 (driven ports IDroneRepository + IAlertNotifier)

## Context

The use case needs to store drones and send alert notifications without knowing about concrete implementations. Clean Architecture prescribes port interfaces for both input (driving) and output (driven) sides. The question was whether the input side needs a formal interface.

## Decision

**Driving port (input):** No interface. The use case exposes `execute(Telemetry)` directly. Callers (infrastructure) depend on the concrete ProcessTelemetry class. There is only one use case, and adding an `IProcessTelemetry` interface would be a one-to-one wrapper with no polymorphism benefit.

**Driven ports (output):** Two interfaces defined in the Domain boundary:
- `IDroneRepository` — `findById(const string&) -> std::optional<Drone>`, `save(const Drone&) -> void`
- `IAlertNotifier` — `notify(const string& drone_id, const vector<AlertTransition>&) -> void`

These are implemented by adapters in Infrastructure (InMemoryDroneRepo, ConsoleAlertNotifier) and by fakes in tests.

## Alternatives Considered

- **Driving port interface (IProcessTelemetry)** — rejected. Only one implementation exists. The interface would add a file, a vtable indirection, and zero testability benefit. If a second use case appears, this can be introduced then.
- **No driven port interfaces (concrete implementations in domain)** — rejected. This would couple domain logic to storage and I/O mechanisms, making unit testing require real infrastructure.
- **Single port combining repository and notifier** — rejected. They are separate concerns with different failure modes and different test doubles.

## Consequences

- **Positive:** Driven port interfaces enable pure domain unit tests with fakes — no infrastructure needed.
- **Positive:** No unnecessary abstraction on the input side keeps the code simple and the call path obvious.
- **Positive:** Port definitions live in Domain, so Infrastructure depends on Domain (correct direction), never the reverse.
- **Negative:** If multiple use cases emerge, the lack of a driving port interface means callers are coupled to concrete types. Acceptable trade-off for a single use case.
