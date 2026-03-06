# ADR-012: Logging with spdlog

**Date:** 2026-03-06
**Status:** Accepted
**Decision:** #30

## Context

The project needs structured, leveled logging across all boundaries — infrastructure logs TCP events, protocol logs CRC failures and resync, domain logs alert transitions, composition root logs startup/shutdown summaries. C++ has no standard logging library. Go has built-in `log`/`slog`; we needed an equivalent.

## Decision

Use **spdlog** via FetchContent (compiled mode, not header-only).

- **Placement:** External build dependency (FetchContent), available to all boundaries. Unlike BlockingQueue (which is project-owned code in the Common utility area), spdlog is a third-party library linked via CMake. It is not part of any architectural boundary — it is a build-time dependency comparable to GTest, made available to all targets via `spdlog::spdlog` link.
- **Integration:** FetchContent in root CMakeLists.txt. All targets link `spdlog::spdlog`.
- **Mode:** Compiled library (not header-only) for faster incremental builds across multiple translation units.

## Alternatives Considered

- **Hand-rolled logger** — rejected. Reimplementing levels, formatting, thread-safe output is wasted effort. spdlog is mature and widely understood.
- **fmt + thin wrapper** — rejected. spdlog already uses fmt internally. Adding fmt alone means building half of spdlog without the logging infrastructure.
- **Header-only spdlog** — rejected for this project. With 6 CMake targets including the library in every TU slows compilation. Compiled mode amortizes the cost.
- **std::cout/cerr directly** — rejected. No log levels, no timestamps, no thread safety, no structured output.

## Consequences

- **Positive:** Consistent, structured logging across all boundaries with zero custom code.
- **Positive:** Thread-safe by default — critical for a multi-threaded pipeline.
- **Positive:** The shutdown summary (packets processed, CRC failures, active drones) becomes a simple `spdlog::info(...)` call.
- **Negative:** Adds a third-party dependency (alongside GTest). Acceptable — spdlog is the de facto standard for C++ logging.
