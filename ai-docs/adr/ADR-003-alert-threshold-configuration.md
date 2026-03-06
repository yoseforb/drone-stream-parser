# ADR-003: Alert Threshold Configuration

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #10 (AlertPolicy with constexpr defaults, injectable)

## Context

Alert thresholds (altitude limit, speed limit) need to be configurable, but the current requirements only define two numbers. The question was how much configuration infrastructure to build: a config file parser, CLI argument parsing, or something simpler.

## Decision

Use an `AlertPolicy` value object with constexpr defaults (120.0m altitude, 50.0 m/s speed). The composition root constructs AlertPolicy and injects it into the use case. The design allows future override from CLI args or a config file without changing any domain code.

**DDD rationale for per-call injection (not a Drone field):** AlertPolicy is a policy object — it governs *how* evaluation happens, but is not intrinsic to the Drone entity. In DDD terms, the Drone owns its evaluation *behavior* (`updateFrom()` logic), but the *thresholds* are system-level configuration. If AlertPolicy were a Drone field, `IDroneRepository` would need to persist and restore it, leaking policy concerns into the repository. Per-call injection keeps entity state clean: the composition root owns the policy, the use case threads it through, and the Drone applies it.

## Alternatives Considered

- **Config file (YAML/JSON/TOML)** — rejected. Adding a parser dependency for two numbers is over-engineering. If needed later, AlertPolicy's constructor is the single injection point.
- **CLI arguments parsed in domain** — rejected. Violates dependency direction. The domain should not know about command-line parsing.
- **Hardcoded constants in Drone** — rejected. Makes thresholds untestable with different values and impossible to override without recompilation.

## Consequences

- **Positive:** Zero configuration infrastructure to build or maintain today.
- **Positive:** Tests can inject different AlertPolicy values to verify threshold behavior at any boundary.
- **Positive:** Future CLI/config file support requires changes only in the composition root — domain code is untouched.
- **Negative:** No runtime configuration without code changes to the composition root (acceptable for current requirements).
