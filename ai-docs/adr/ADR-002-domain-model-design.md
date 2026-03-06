# ADR-002: Domain Model Design

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #5 (flat fields), #6 (rich entity), #7 (hybrid alert model), #8 (set-based alert state), #9 (vector<AlertTransition> return)

## Context

The domain needs to model drones receiving telemetry updates and generating alerts when thresholds are breached. Key design tensions: whether to extract a Position value object from Drone's coordinate fields, whether Drone should be a rich or anemic entity, and how to model alert state — specifically whether to use enums, booleans, or a set, and how to communicate state changes to the use case.

The domain model follows DDD (Domain-Driven Design) principles: Entities have identity and own their state + behavior. Value Objects are immutable data carriers with no identity. Policy objects (like AlertPolicy) are external configuration — they govern how evaluation happens but are not intrinsic to any entity. This distinction matters for the Drone entity: the Drone owns its alert evaluation *behavior* (the `updateFrom()` logic) but does not own the alert *thresholds* (AlertPolicy). Thresholds are system-level configuration injected per-call, keeping the entity's state clean and avoiding policy storage concerns in the repository.

## Decision

**Drone entity (rich, identity by drone_id):**
- Flat fields for lat, lon, alt, speed, timestamp — no Position value object (YAGNI, no position-specific behavior exists).
- Rich entity that owns `updateFrom(Telemetry, AlertPolicy) -> vector<AlertTransition>` as a noexcept method.
- Alert state stored as `std::set<AlertType>` — extensible to new alert types without combinatorial explosion.
- `updateFrom()` returns `vector<AlertTransition>` indicating which alerts were entered or cleared. The Drone reports what changed; the use case decides what to do about it.

**Hybrid alert model:**
- Drone tracks current alert state (set membership).
- Use case (ProcessTelemetry) decides when to notify (only on transitions, not on every update).

**Value objects:**
- `Telemetry` — immutable snapshot with drone_id, lat, lon, alt, speed, timestamp.
- `AlertType` — enum: ALTITUDE, SPEED.
- `AlertTransition` — struct: `{ AlertType type, bool entered }`.

## Alternatives Considered

- **Position value object** — rejected. No behavior operates on position as a unit (no distance calculations, no geofencing). Extracting it would add a type with no methods, just to hold three fields already on Drone.
- **Anemic entity + external alert service** — rejected. Alert evaluation is intrinsic to Drone state; putting it elsewhere would scatter the logic and require exposing Drone internals.
- **Boolean flags per alert type** — rejected. Adding a new alert type would require adding a new field, a new getter, and updating every comparison. A set scales to N alert types with zero structural changes.
- **Drone returns bool (has-alerts) instead of transitions** — rejected. The use case needs to know which specific alerts changed to generate meaningful notifications. A boolean loses that information.
- **Use case tracks alert state** — rejected. This splits the responsibility: Drone would hold telemetry, use case would hold alerts. The Drone is the natural owner of "what state am I in."

## Consequences

- **Positive:** Drone is self-contained — given telemetry and policy, it produces a complete result. Easy to unit test with no mocks.
- **Positive:** `set<AlertType>` makes adding BATTERY_LOW or GEOFENCE a one-line enum extension.
- **Positive:** `vector<AlertTransition>` gives the use case precise, actionable information without coupling it to Drone internals.
- **Negative:** Flat fields mean if position-specific behavior is ever needed (distance, geofencing), a refactor to extract Position would touch Drone's interface.
