# ADR-008: Wire Format and CRC

**Date:** 2026-03-05
**Status:** Accepted
**Decisions:** #14 (CRC-16/CCITT internal to protocol), #25 (uint16_t string prefix, little-endian)

## Context

The spec defines a packet format with header, length, payload, and CRC fields but leaves some serialization details open: how to encode variable-length strings, what byte order to use, and which CRC variant to implement. These choices affect parser complexity and cross-platform compatibility.

## Decision

**Frame layout:**
```
[0xAA 0x55] [LENGTH:u16 LE] [PAYLOAD:N bytes] [CRC:u16 LE]
```

**Telemetry payload serialization:**
- String prefix: `uint16_t` little-endian byte count (consistent with the LENGTH field's type).
- All numeric fields: little-endian (matches x86-64 target, no byte-swap needed).
- Doubles: IEEE 754 little-endian.
- Timestamp: `uint64_t` LE, Unix epoch milliseconds.
- Total payload: `42 + id_len` bytes.

**CRC variant:** CRC-16/CCITT (polynomial 0x1021, init 0x0000). Table-driven implementation. CRC is computed over header + length + payload bytes. Internal to Protocol boundary.

## Alternatives Considered

- **Network byte order (big-endian)** — rejected. The target is x86-64 Linux. Big-endian would require byte-swapping on every field for no interoperability benefit (both client and server run on the same platform).
- **Null-terminated strings** — rejected. Requires scanning for a sentinel byte during parsing. Length-prefixed strings allow pre-allocating the exact buffer size and are unambiguous.
- **uint8_t string prefix** — rejected. Limits drone IDs to 255 bytes. uint16_t is consistent with the frame's LENGTH field and costs only one extra byte.
- **CRC-32** — rejected. CRC-16 is sufficient for the packet sizes involved (max payload 4096 bytes). CRC-32 adds 2 bytes per packet with no practical benefit at this scale.
- **CRC in domain** — rejected. CRC is a wire-format integrity check, purely a protocol concern. Domain should not know about byte-level encoding.

## Consequences

- **Positive:** Little-endian on x86-64 means `memcpy` directly into native types — no conversion code, no byte-swap bugs.
- **Positive:** Length-prefixed strings make payload parsing O(1) for the string field (read length, read exactly that many bytes).
- **Positive:** CRC-16/CCITT is well-documented, table-driven implementation is fast, and the polynomial is widely used in embedded protocols.
- **Negative:** Not portable to big-endian architectures without adding byte-swap logic. Acceptable — the project targets x86-64 Linux.
