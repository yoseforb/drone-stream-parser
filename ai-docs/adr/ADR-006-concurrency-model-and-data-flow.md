# ADR-006: Concurrency Model and Data Flow

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #15 (3-stage pipeline), #16 (bounded queues with move semantics), #17 (composition root ownership), #20 (cascade shutdown)

## Context

The spec requires 3+ threads and thread-safe queues between layers. Rather than assigning threads first and fitting data flow around them, the design started from data flow: bytes arrive from TCP, get parsed into Telemetry, then get processed by domain logic. This naturally maps to a 3-stage pipeline. The approach was compared to Go channels — bounded queues connecting stages, with each stage as a goroutine (here, a thread).

The spec does not specify whether queues should be bounded or unbounded, nor what should happen when a stage falls behind (overflow strategy). These are design decisions that need explicit rationale.

## Decision

**3-stage pipeline (data flow first, threads follow):**
```
TCP recv -> Q1 (raw bytes) -> parse -> Q2 (Telemetry) -> process
```

### Queues

- Q1: `BlockingQueue<vector<uint8_t>>` — raw byte chunks
- Q2: `BlockingQueue<Telemetry>` — parsed telemetry objects
- Elements transferred by value with `std::move`. Both `vector<uint8_t>` and `Telemetry` are move-efficient (O(1) pointer swap). No `unique_ptr` wrapping needed.

### Bounded queues with blocking overflow (back-pressure)

This is two decisions:

**1. Bounded, not unbounded.** If a downstream stage is slower than upstream, an unbounded queue grows without limit until memory is exhausted. In a drone/embedded domain this is unacceptable. Bounded queues cap memory usage and make the system self-regulating.

**2. Block when full (back-pressure), not drop.** A bounded queue needs an overflow policy — what happens when a producer tries to push to a full queue? Two options:

- **Block (back-pressure):** The producer thread sleeps until the consumer frees a slot. The entire pipeline slows to match the bottleneck. No data is lost.
- **Drop:** Discard the newest or oldest element. The producer never blocks. Data is lost but the pipeline stays at full speed.

**Blocking chosen because:**
- Simplest to implement correctly — `condition_variable::wait()` on push, no decision logic about what to discard.
- Preserves packet ordering and completeness — no silent data loss.
- The spec requires 1000 pps throughput. If the system meets this, back-pressure rarely activates under normal conditions. Overload is not the expected operating mode.

**How back-pressure propagates:** When the process stage (Thread 3) is slow, the cascade is:
1. Q2 fills → parse thread blocks on `q2.push()` inside the parser callback (see note below)
2. Q1 fills → recv thread blocks on `q1.push()`
3. Recv stops calling `recv()` → OS TCP buffer fills → kernel advertises zero window → sender stops transmitting

All blocked threads sleep on condition variables (zero CPU). Only the bottleneck thread runs. The system self-regulates.

**Note on parser callback coupling:** The parse thread calls `parser.feed(chunk)`, and the parser invokes a callback on each valid packet. That callback pushes to Q2. When Q2 is full, the callback blocks, which means `feed()` blocks mid-parse. This is correct — the parser's internal buffer holds unprocessed bytes consistently, and parsing resumes exactly where it left off when the callback unblocks. This coupling between back-pressure and the parser callback is intentional.

**Production consideration:** If the system were deployed in a real-time tracking scenario where freshness matters more than completeness, a drop-oldest or per-drone conflation strategy could replace blocking. That trade-off (lose data, gain freshness) is driven by operational requirements that the spec doesn't define. Blocking is the safer default — it preserves all data and makes overload visible rather than hiding it behind silent drops.

### Ownership

- Composition root (`main()`) creates queues on the stack.
- Pipeline stages receive `BlockingQueue<T>&` via constructor injection.
- Stages borrow, never own. RAII lifetime is guaranteed by stack ordering in main().

### Graceful shutdown (cascade)

```
Signal (SIGINT/SIGTERM) -> atomic<bool> stop_flag
  -> Recv stage: sees flag, closes socket, calls Q1.close()
  -> Parse stage: Q1.pop() returns nullopt, calls Q2.close()
  -> Process stage: Q2.pop() returns nullopt, exits
  -> main() joins threads in pipeline order
```
No data silently dropped — each stage drains its input before closing its output.

## Alternatives Considered

- **2 threads (I/O + processing)** — rejected. Spec requires 3+ threads. Also, separating parsing from domain processing is a clean boundary that would be lost.
- **Thread pool / task-based** — rejected. The workload is a linear pipeline, not a fork-join graph. A thread pool adds scheduling complexity without matching the data flow shape.
- **Unbounded queues** — rejected. If a stage falls behind, unbounded queues grow until memory is exhausted. Bounded queues cap memory and make the system self-regulating.
- **Drop-on-overflow (newest or oldest)** — not chosen. Trades completeness for freshness — useful when operational requirements prioritize latest data over processing every packet. Adds decision logic (which element to discard, per-drone conflation) that the spec doesn't require. Blocking is the safer default for the specified 1000 pps load.
- **shared_ptr for queue ownership** — rejected. Queue lifetime is deterministic (stack in main). Reference injection is simpler, with no overhead and no shared ownership ambiguity.
- **Shutdown via thread cancellation / pthread_cancel** — rejected. Cascade through queue close is clean, cooperative, and doesn't require platform-specific cancellation mechanisms.

## Consequences

- **Positive:** Data flow is obvious from the pipeline structure. Each stage has a single responsibility.
- **Positive:** Bounded queues with blocking provide natural back-pressure. The entire pipeline self-regulates to match the slowest stage, using zero CPU for waiting threads.
- **Positive:** Cascade shutdown is deterministic — no race conditions, no data loss, no forced kills.
- **Positive:** Move semantics mean zero-copy transfer through queues for both byte buffers and Telemetry objects.
- **Negative:** Fixed 3-stage pipeline. If a stage needs to be split (e.g., multiple parsers), the pipeline structure needs rework. Acceptable given the domain size.
- **Negative:** Back-pressure means stale data is processed during overload rather than dropped. Acceptable because the system is designed to handle the specified load (1000 pps), so overload is not the normal operating condition.
