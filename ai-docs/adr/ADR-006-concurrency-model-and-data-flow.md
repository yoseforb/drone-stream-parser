# ADR-006: Concurrency Model and Data Flow

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #15 (3-stage pipeline), #16 (bounded queues with move semantics), #17 (composition root ownership), #20 (cascade shutdown)

## Context

The spec requires 3+ threads. Rather than assigning threads first and fitting data flow around them, the design started from data flow: bytes arrive from TCP, get parsed into Telemetry, then get processed by domain logic. This naturally maps to a 3-stage pipeline. The approach was compared to Go channels — bounded queues connecting stages, with each stage as a goroutine (here, a thread).

## Decision

**3-stage pipeline (data flow first, threads follow):**
```
TCP recv -> Q1 (raw bytes) -> parse -> Q2 (Telemetry) -> process
```

**Queues:**
- Q1: `BlockingQueue<vector<uint8_t>>` — raw byte chunks
- Q2: `BlockingQueue<Telemetry>` — parsed telemetry objects
- Bounded — back-pressure prevents memory growth (correct for embedded domain)
- Elements transferred by value with `std::move`. Both `vector<uint8_t>` and `Telemetry` are move-efficient (O(1) pointer swap). No `unique_ptr` wrapping needed.

**Ownership:**
- Composition root (`main()`) creates queues on the stack.
- Pipeline stages receive `BlockingQueue<T>&` via constructor injection.
- Stages borrow, never own. RAII lifetime is guaranteed by stack ordering in main().

**Graceful shutdown (cascade):**
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
- **Unbounded queues** — rejected. In an embedded/drone domain, memory growth without back-pressure is unacceptable. Bounded queues make the system self-regulating.
- **shared_ptr for queue ownership** — rejected. Queue lifetime is deterministic (stack in main). Reference injection is simpler, with no overhead and no shared ownership ambiguity.
- **Shutdown via thread cancellation / pthread_cancel** — rejected. Cascade through queue close is clean, cooperative, and doesn't require platform-specific cancellation mechanisms.

## Consequences

- **Positive:** Data flow is obvious from the pipeline structure. Each stage has a single responsibility.
- **Positive:** Bounded queues provide natural back-pressure — if processing is slow, recv blocks, which is the correct behavior.
- **Positive:** Cascade shutdown is deterministic — no race conditions, no data loss, no forced kills.
- **Positive:** Move semantics mean zero-copy transfer through queues for both byte buffers and Telemetry objects.
- **Negative:** Fixed 3-stage pipeline. If a stage needs to be split (e.g., multiple parsers), the pipeline structure needs rework. Acceptable given the domain size.
