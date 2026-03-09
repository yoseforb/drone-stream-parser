# ADR-009: State Machine Parser Design

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #26 (5-state machine, rewind resync), #27 (parser stats internal)

## Context

The parser must handle fragmented TCP delivers (packets split across recv() calls), corrupted data (bad CRC, garbage bytes), and resynchronization after errors. A byte-at-a-time state machine is the standard approach for stream protocol parsing, but the resync strategy on error and the handling of parser statistics needed explicit decisions.

## Decision

**5-state machine:**
```
HUNT_HEADER -> READ_LENGTH -> READ_PAYLOAD -> READ_CRC -> COMPLETE_FRAME -> HUNT_HEADER
```

1. **HUNT_HEADER** — scan byte-by-byte for 0xAA then 0x55.
2. **READ_LENGTH** — read 2 bytes into uint16_t. If > MAX_PAYLOAD (4096): resync.
3. **READ_PAYLOAD** — buffer exactly `length` bytes.
4. **READ_CRC** — read 2 bytes, compute CRC over header + length + payload.
   - Match: advance to COMPLETE_FRAME.
   - Mismatch: increment crc_fail_count, resync.
5. **COMPLETE_FRAME** — deserialize the payload into a Telemetry struct, compact the buffer, reset state, and invoke the callback. Return to HUNT_HEADER.
   - Deserialization failure: resync.

**Resync strategy:** On CRC failure, invalid length, or deserialization failure in COMPLETE_FRAME, rewind the buffer read position to one byte after the 0xAA that started the current packet attempt. The "header" that was found may have been random data matching 0xAA55. Re-enter HUNT_HEADER to scan for the next real sync point. This is O(n) and minimizes data loss.

**MAX_PAYLOAD guard (4096):** Prevents a malformed length field from causing unbounded memory allocation.

**Internal accumulation buffer:** The StreamParser maintains an internal `std::vector<uint8_t>` accumulation buffer. When `feed()` is called, incoming bytes are appended to this buffer. The state machine operates on the buffer contents, tracking a read cursor. On successful packet parse, consumed bytes are erased from the front of the buffer. On resync (CRC failure or invalid length), the read cursor rewinds within this buffer to one byte after the `0xAA` that started the failed attempt, and parsing resumes from there. This buffer is essential — without it, rewind-based resync would be impossible since raw bytes from previous `feed()` calls would already be gone.

**Parser stats:** The parser tracks `crc_fail_count` and `malformed_count` internally via getter methods. These are logged by the composition root at shutdown. Stats are not a domain concern — they are protocol-level diagnostics.

## Alternatives Considered

- **Skip-forward resync (advance past the failed packet)** — rejected. If the "header" was actually mid-stream data, skipping forward could miss a real packet that starts within the failed region. Rewind-to-header+1 re-examines all bytes.
- **Separate resync state** — rejected. Resync is just "go back to HUNT_HEADER with adjusted read position." Adding a dedicated resync state would complicate the state machine without adding clarity.
- **Parser stats as a separate observer/listener** — rejected. Two counters do not justify an observer pattern. Getters on the parser are simple and sufficient.
- **Expose stats via domain events** — rejected. CRC failure counts are protocol diagnostics, not domain events. They should not flow through the domain boundary.
- **No MAX_PAYLOAD guard** — rejected. A malformed length of 0xFFFF would attempt to allocate 65KB, and repeated corruption could exhaust memory. The guard provides a safety bound.

## Consequences

- **Positive:** The state machine handles all edge cases: fragmented delivery, mid-stream corruption, garbage bytes, and consecutive bad packets.
- **Positive:** Rewind resync minimizes data loss — at most one valid packet could be missed if its header bytes happened to overlap with the failed packet's data.
- **Positive:** `feed()` is noexcept — it processes bytes and emits Telemetry via callback, never fails.
- **Positive:** MAX_PAYLOAD guard prevents resource exhaustion from malformed data.
- **Negative:** Rewind resync re-processes bytes, which is slightly less efficient than skip-forward. For packet sizes under 4KB, this is negligible.
- **Negative:** The callback runs inside the `noexcept` boundary of `feed()`. A throwing callback triggers `std::terminate()`. The callback contract (must not throw, may block) must be documented in the StreamParser interface.
