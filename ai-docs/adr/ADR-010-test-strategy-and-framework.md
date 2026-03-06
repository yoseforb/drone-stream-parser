# ADR-010: Test Strategy and Framework

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #2 (GTest + GMock), #13 (client as separate binary)

## Context

The project follows TDD with Interface-Driven Design (IDD). Needed to choose between zero-dependency testing (hand-rolled assertions) and a framework. Also needed to decide how to test infrastructure (TCP, threads, OS interaction) — specifically whether to mock sockets and threads or test them via a real client.

## Decision

**Test framework:** GTest + GMock, fetched via CMake FetchContent. GTest provides test infrastructure (assertions, fixtures, test discovery). GMock provides mock generation for external interface verification where needed.

**Test strategy by boundary:**
- **Domain:** Pure unit tests using hand-written fakes (FakeDroneRepository, FakeAlertNotifier). Fakes are preferred over mocks for internal interfaces because they are simpler, more readable, and test behavior rather than interaction.
- **Protocol:** Unit tests feeding raw byte arrays directly to the parser and serializer. No mocks needed — input is bytes, output is Telemetry or bytes.
- **Infrastructure:** Integration tested via a separate client binary (`drone_client`). No unit tests for TcpServer, SignalHandler, or thread wiring. Mocking sockets and threads adds complexity without catching real bugs (the bugs live in actual OS interaction).

**Client binary:** Separate executable, independent of the server architecture. It is a test tool, not part of the system under test. It connects via TCP and exercises the full stack.

**Integration verification:** Structured console output + shutdown summary (total packets processed, CRC failures, active drones, alerts triggered). No query API or programmatic verification layer — the examiner reads the server output. Unit tests prove component correctness; the client demonstrates the system works end-to-end.

## Alternatives Considered

- **Zero-dependency testing (hand-rolled framework)** — rejected. Re-implementing test discovery, assertion macros, and failure reporting is wasted effort when GTest is mature and fetched in one CMake line.
- **Catch2** — viable alternative, but GTest + GMock is a single ecosystem. GMock's mock generation is useful for port interface verification if fakes become insufficient.
- **Mocking TCP sockets** — rejected. Socket mocks test that you called `recv()` with the right arguments, not that your server actually handles TCP correctly. Integration via a real client catches real bugs.
- **Client as part of the test executable** — rejected. The client needs its own main(), its own CLI arguments, and runs as a separate process connecting via TCP. It is not a unit test.

## Consequences

- **Positive:** Domain tests are fast, isolated, and require zero infrastructure setup.
- **Positive:** Protocol tests catch parser bugs with precise byte-level control.
- **Positive:** Client-based integration tests catch real infrastructure bugs (port binding, TCP framing, thread lifecycle).
- **Positive:** GTest integrates with CTest, CI systems, and IDEs out of the box.
- **Negative:** Integration tests (client-server) are slower and require starting the server. Acceptable — they test a different category of bugs than unit tests.
- **Negative:** FetchContent adds a build-time download step. Acceptable — it is cached after the first build.
