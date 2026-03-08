# Phase 3: Protocol Boundary — Implementation Plan

**Date:** 2026-03-08
**Status:** Active
**Branch:** phase-2-domain-boundary (continue here; phase 3 is next)
**Standard:** C++20 | GCC 15.2.1 | CMake 4.2.3 | Linux

---

## Overview

Phase 3 implements the Protocol boundary: CRC16, PacketSerializer, and StreamParser.
These are the three components that convert raw TCP bytes into validated `Telemetry`
values and back. This is the most bug-prone code in the system — it handles fragmented
TCP delivers, corrupted bytes, and state machine recovery. It is fully TDD'd before any
infrastructure exists.

**Depends on:** Phase 2 (Domain boundary) — uses the `Telemetry` struct from
`domain/include/telemetry.hpp`.

**Dependency direction:** Protocol → Domain only. No infrastructure dependencies.

**TDD workflow per component:** interface → stub (compile fails tests) → tests RED →
implementation → tests GREEN.

**Within-phase ordering is a hard constraint:**
1. CRC16 (no dependencies within Protocol)
2. PacketSerializer (depends on CRC16)
3. StreamParser (depends on CRC16; tests use PacketSerializer for round-trip fixtures)

---

## Current State

- `protocol/CMakeLists.txt` — declares `protocol` STATIC lib, links `domain`, lists
  only `src/stub.cpp` as a source
- `protocol/src/stub.cpp` — placeholder comment, no real code
- `protocol/include/.gitkeep` — empty placeholder; must be removed when real headers arrive
- `tests/CMakeLists.txt` — lists only domain test files; protocol test files must be added
- `tests/protocol/.gitkeep` — empty placeholder; must be removed when test files arrive

---

## Architecture Constraints (apply to every file in this phase)

### Include guards
Use `#ifndef` guards — NOT `#pragma once`. clang-tidy enforces `llvm-header-guard`.
Pattern: `#ifndef FILENAME_HPP` / `#define FILENAME_HPP` / `#endif // FILENAME_HPP`

### Naming conventions
- Classes: `CamelCase` (`StreamParser`, `PacketSerializer`)
- Free functions: `camelBack` (`crc16`)
- Member variables: `snake_case_` with trailing underscore (`buffer_`, `read_pos_`, `state_`)
- Public struct fields: `snake_case` without trailing underscore (matching `Telemetry`)
- Constants/constexpr: `CamelCase` (`MaxPayload`)
- Enum class values: `UPPER_CASE` (`HUNT_HEADER`, `READ_LENGTH`)

### `[[nodiscard]]`
Required on all getters and any function whose return value must not be silently ignored.
clang-tidy enforces this. Apply to: `crc16()`, `serialize()`, `getCrcFailCount()`,
`getMalformedCount()`.

### Magic numbers
Extract every raw numeric literal to a named `constexpr`. Examples:
`constexpr uint16_t HeaderByte0 = 0xAAU;`
`constexpr uint16_t HeaderByte1 = 0x55U;`
`constexpr uint16_t MaxPayload = 4096U;`
`constexpr size_t LengthFieldSize = 2U;`
`constexpr size_t CrcFieldSize = 2U;`
`constexpr size_t HeaderSize = 2U;`

### noexcept boundary
- `crc16()` — `noexcept` (pure arithmetic)
- `StreamParser::feed()` — `noexcept` (state machine always progresses)
- `StreamParser::getCrcFailCount()` — `noexcept`
- `StreamParser::getMalformedCount()` — `noexcept`
- `PacketSerializer::serialize()` — NOT `noexcept` (calls `std::vector` allocation)

### Include what you use
`misc-include-cleaner` enforces this. Include every header directly used in that file;
do not rely on transitive includes.

### clang-tidy member reference rule
`cppcoreguidelines-avoid-const-or-ref-data-members` — store non-owning references as
pointers, not references. The `StreamParser` callback is a `std::function` value member
(not a reference), so this does not apply there.

### Test file magic numbers
Wrap test-only numeric literals in `// NOLINTBEGIN(readability-magic-numbers)` /
`// NOLINTEND(readability-magic-numbers)` blocks, following the pattern in
`tests/domain/drone_test.cpp`.

---

## Wire Format Reference (ADR-008)

```
Offset  Size     Field    Notes
------  ----     -----    -----
0       1        0xAA     Fixed header byte 0 (not an integer — endianness does not apply)
1       1        0x55     Fixed header byte 1
2       2        LENGTH   uint16_t LE — byte count of PAYLOAD only
4       LENGTH   PAYLOAD  Serialized Telemetry (see below)
4+L     2        CRC      uint16_t LE — CRC-16/CCITT over bytes [0 .. 4+LENGTH-1]
```

**Payload layout (total: `42 + id_len` bytes):**

```
Offset      Size    Field       Encoding
------      ----    -----       --------
0           2       id_len      uint16_t LE — byte count of drone_id string
2           id_len  drone_id    UTF-8, no null terminator
2+id_len    8       latitude    IEEE 754 double, LE
10+id_len   8       longitude   IEEE 754 double, LE
18+id_len   8       altitude    IEEE 754 double, LE
26+id_len   8       speed       IEEE 754 double, LE
34+id_len   8       timestamp   uint64_t LE, Unix epoch milliseconds
```

---

## CRC-16/CCITT Specification (ADR-008)

| Parameter     | Value                                    |
|---------------|------------------------------------------|
| Name          | CRC-16/XMODEM (also CRC-16/CCITT-FALSE) |
| Polynomial    | 0x1021                                   |
| Initial value | 0x0000                                   |
| Input reflect | No                                       |
| Output reflect| No                                       |
| Final XOR     | 0x0000 (none)                            |

**Precomputed test vectors** (verified with reference implementation):

| Input                          | Expected CRC |
|--------------------------------|--------------|
| Empty (zero bytes)             | 0x0000       |
| `{0x00}`                       | 0x0000       |
| `{0x31}` (ASCII '1')           | 0x2672       |
| `{0xFF}`                       | 0x1EF0       |
| `{0x48,0x65,0x6C,0x6C,0x6F}` ("Hello") | 0xCBD6 |
| `"123456789"` (9 bytes)        | 0x31C3 (standard XMODEM check value) |

**Packet-level CRC test vectors** (CRC over full frame: header + length + payload):

| Packet                                                              | Payload bytes | CRC    |
|---------------------------------------------------------------------|---------------|--------|
| `drone_id="D1"`, lat=1.0, lon=2.0, alt=3.0, speed=4.0, ts=1000    | 44            | 0x5CAA |
| `drone_id="DRONE-001"`, lat=51.5074, lon=-0.1278, alt=100.0, speed=25.0, ts=1700000000000 | 51 | 0xF7F8 |

---

## Step 1 — Remove placeholders

**Files modified:**
- Delete `protocol/include/.gitkeep`
- Delete `tests/protocol/.gitkeep`

These are empty placeholders. Real header and test files in their directories make them
redundant. Deleting them avoids clang-tidy's `misc-include-cleaner` picking up an
unexpected file.

**Build verification:** `cmake --build --preset=dev` — must still pass (no protocol
sources changed yet, `stub.cpp` still the only source).

---

## Step 2 — 3a: CRC16 Header

**Files created:**
- `protocol/include/crc16.hpp`

**Content requirements:**

```
#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>
#include <span>

[[nodiscard]] uint16_t crc16(std::span<const uint8_t> data) noexcept;

#endif // CRC16_HPP
```

Key points:
- `[[nodiscard]]` — caller must not silently discard the checksum result
- `noexcept` — pure arithmetic, cannot fail
- `std::span<const uint8_t>` — zero-copy view, C++20 idiom (matches ADR-008 decision)
- No namespace — free function, matches `crc16()` naming in all architecture docs
- No implementation in this header — declared here, defined in `crc16.cpp`

**Dependencies:** None (this step has no prerequisites beyond Phase 2).

**Build verification:** Not yet — `crc16.cpp` does not exist yet so the build would fail
to link. Verify only after Step 3.

---

## Step 3 — 3a: CRC16 Stub

**Files modified:**
- `protocol/src/crc16.cpp` (new file — replaces `stub.cpp` role once added to CMake)
- `protocol/CMakeLists.txt` (add `src/crc16.cpp` to sources, keep `src/stub.cpp` for now)

**`protocol/src/crc16.cpp` stub content:**

```cpp
#include "crc16.hpp"

#include <cstdint>
#include <span>

uint16_t crc16(std::span<const uint8_t> /*data*/) noexcept { return 0U; }
```

Key points:
- Include `"crc16.hpp"` first (own header first — clang-tidy enforces this)
- Parameter named `/*data*/` with comment to suppress unused-parameter warning
- Returns `0U` — stub, intentionally wrong, tests will fail RED

**`protocol/CMakeLists.txt` change:** Add `src/crc16.cpp` to the `add_library` sources.
`stub.cpp` stays until Step 16 (removed when `stream_parser.cpp` is added and stub is
no longer needed).

**Build verification:** `cmake --build --preset=dev` — must compile and link cleanly.
Zero warnings, zero clang-tidy diagnostics.

---

## Step 4 — 3a: CRC16 Tests (RED)

**Files created:**
- `tests/protocol/crc16_test.cpp`

**`tests/CMakeLists.txt` change:** Add `protocol/crc16_test.cpp` to the `tests`
executable source list.

**Test file structure:**

```cpp
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"

// NOLINTBEGIN(readability-magic-numbers)

TEST(Crc16Test, EmptySpanReturnsZero) { ... }
TEST(Crc16Test, SingleByteZeroReturnsZero) { ... }
TEST(Crc16Test, SingleByte0x31Returns0x2672) { ... }
TEST(Crc16Test, SingleByte0xFFReturns0x1EF0) { ... }
TEST(Crc16Test, HelloBytesReturn0xCBD6) { ... }
TEST(Crc16Test, Digits123456789ReturnStandardXmodemCheckValue) { ... }

// NOLINTEND(readability-magic-numbers)
```

**Detailed test specifications:**

**`EmptySpanReturnsZero`**
- Input: `std::span<const uint8_t>{}` (empty)
- Expected: `0x0000`
- Rationale: CRC-16/XMODEM init=0x0000, no bytes processed → result is init value

**`SingleByteZeroReturnsZero`**
- Input: `std::array<uint8_t, 1>{0x00}`
- Expected: `0x0000`
- Rationale: 0x00 XORed into high byte → 0x0000, 8 shifts with no high bit set → 0x0000

**`SingleByte0x31Returns0x2672`**
- Input: `std::array<uint8_t, 1>{0x31}`
- Expected: `0x2672`
- Rationale: precomputed with reference CRC-16/XMODEM algorithm

**`SingleByte0xFFReturns0x1EF0`**
- Input: `std::array<uint8_t, 1>{0xFF}`
- Expected: `0x1EF0`
- Rationale: precomputed with reference algorithm

**`HelloBytesReturn0xCBD6`**
- Input: `std::vector<uint8_t>{0x48, 0x65, 0x6C, 0x6C, 0x6F}` ("Hello")
- Expected: `0xCBD6`

**`Digits123456789ReturnStandardXmodemCheckValue`**
- Input: `std::vector<uint8_t>{0x31, 0x32, 0x33, 0x34, 0x35, 0x36, 0x37, 0x38, 0x39}`
- Expected: `0x31C3`
- Rationale: this is the internationally standardized CRC-16/XMODEM check value for
  the string "123456789" — if the implementation is wrong this test catches it

**RED verification:** `cmake --build --preset=dev && ctest --preset=dev`
All `Crc16Test.*` tests fail (stub returns 0 for all inputs, only `EmptySpanReturnsZero`
passes coincidentally). Confirm RED before proceeding.

---

## Step 5 — 3a: CRC16 Implementation (GREEN)

**Files modified:**
- `protocol/src/crc16.cpp` (replace stub with table-driven implementation)

**Implementation approach:** 256-entry precomputed lookup table, CRC-16/CCITT poly
0x1021, init 0x0000, no reflection, no final XOR.

**Implementation structure:**

```cpp
#include "crc16.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace {

// Table-driven CRC-16/CCITT (XMODEM): poly=0x1021, init=0x0000, no reflection
constexpr std::array<uint16_t, 256> buildTable() noexcept {
    std::array<uint16_t, 256> table{};
    for (uint16_t i = 0; i < 256U; ++i) {
        uint16_t crc = static_cast<uint16_t>(i << 8U);
        for (int bit = 0; bit < 8; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<uint16_t>((crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<uint16_t>(crc << 1U);
            }
        }
        table[i] = crc;
    }
    return table;
}

constexpr std::array<uint16_t, 256> CrcTable = buildTable();

} // namespace

uint16_t crc16(std::span<const uint8_t> data) noexcept {
    uint16_t crc = 0x0000U;
    for (uint8_t const byte : data) {
        uint8_t const index = static_cast<uint8_t>(
            (crc >> 8U) ^ byte);
        crc = static_cast<uint16_t>((crc << 8U) ^ CrcTable[index]);
    }
    return crc;
}
```

Key points:
- `buildTable()` is `constexpr` — table built at compile time, zero runtime cost
- Table is in anonymous namespace — internal linkage, not exposed
- All shifts and XORs cast back to `uint16_t` — avoids integer-promotion warnings
- No heap allocation, no I/O — `noexcept` is correct
- `const byte` in range-for — clang-tidy `readability-const-return-type` compliance

**GREEN verification:** `cmake --build --preset=dev && ctest --preset=dev`
All `Crc16Test.*` must pass. Zero warnings, zero clang-tidy diagnostics.

---

## Step 6 — 3b: PacketSerializer Header

**Files created:**
- `protocol/include/packet_serializer.hpp`

**Content requirements:**

```cpp
#ifndef PACKET_SERIALIZER_HPP
#define PACKET_SERIALIZER_HPP

#include <cstdint>
#include <vector>

#include "telemetry.hpp"

class PacketSerializer {
public:
    [[nodiscard]] std::vector<uint8_t> serialize(const Telemetry& tel);
};

#endif // PACKET_SERIALIZER_HPP
```

Key points:
- `[[nodiscard]]` — caller must use the returned packet bytes
- NOT `noexcept` — `std::vector` allocation may throw `std::bad_alloc`
- No constructor needed — stateless class, default constructor suffices
- `#include "telemetry.hpp"` — directly uses `Telemetry`; include-cleaner requires it
- `#include "crc16.hpp"` goes in the `.cpp` not the `.hpp` — CRC is an implementation
  detail, not part of the public interface

**Dependencies:** Step 2 (`crc16.hpp` must exist, used in `.cpp`), Phase 2 (`telemetry.hpp`).

---

## Step 7 — 3b: PacketSerializer Stub

**Files created:**
- `protocol/src/packet_serializer.cpp`

**`protocol/src/packet_serializer.cpp` stub content:**

```cpp
#include "packet_serializer.hpp"

#include <cstdint>
#include <vector>

#include "telemetry.hpp"

std::vector<uint8_t> PacketSerializer::serialize(const Telemetry& /*tel*/) {
    return {};
}
```

**`protocol/CMakeLists.txt` change:** Add `src/packet_serializer.cpp` to sources.

**Build verification:** `cmake --build --preset=dev` — must compile and link cleanly.

---

## Step 8 — 3b: PacketSerializer Tests (RED)

**Files created:**
- `tests/protocol/serializer_test.cpp`

**`tests/CMakeLists.txt` change:** Add `protocol/serializer_test.cpp` to the `tests`
executable source list.

**Test file structure:**

```cpp
#include <array>
#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"
#include "packet_serializer.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)
namespace {

Telemetry makeSimpleTelemetry() {
    return {.drone_id  = "D1",
            .latitude  = 1.0,
            .longitude = 2.0,
            .altitude  = 3.0,
            .speed     = 4.0,
            .timestamp = 1000U};
}

} // namespace
// NOLINTEND(readability-magic-numbers)

TEST(PacketSerializerTest, OutputStartsWithHeaderBytes) { ... }
TEST(PacketSerializerTest, LengthFieldMatchesPayloadSize) { ... }
TEST(PacketSerializerTest, PayloadStartsWithDroneIdLengthPrefix) { ... }
TEST(PacketSerializerTest, PayloadContainsDroneIdBytes) { ... }
TEST(PacketSerializerTest, LatitudeEncodedAsLittleEndianDouble) { ... }
TEST(PacketSerializerTest, TimestampEncodedAsLittleEndianUint64) { ... }
TEST(PacketSerializerTest, CrcAtEndMatchesCrc16OverFullFrame) { ... }
TEST(PacketSerializerTest, TotalSizeIs6PlusPayloadSize) { ... }
```

**Detailed test specifications:**

**`OutputStartsWithHeaderBytes`**
- Serialize any `Telemetry` (use `makeSimpleTelemetry()`)
- `ASSERT_GE(packet.size(), 2U)`
- `EXPECT_EQ(packet[0], 0xAAU)`
- `EXPECT_EQ(packet[1], 0x55U)`

**`LengthFieldMatchesPayloadSize`**
- Serialize `makeSimpleTelemetry()` (`drone_id="D1"`, 2 bytes)
- Payload size = 2 (id_len) + 2 (drone_id bytes) + 8×4 (doubles) + 8 (timestamp) = 44
- Read LENGTH as `uint16_t` LE from `packet[2..3]`
- `EXPECT_EQ(length, 44U)`
- Also verify `packet[2] == 0x2C` and `packet[3] == 0x00` (44 = 0x2C LE)

**`PayloadStartsWithDroneIdLengthPrefix`**
- Payload starts at `packet[4]`
- Read 2 bytes LE as `uint16_t` → should equal `2U` (length of "D1")
- `EXPECT_EQ(packet[4], 0x02U)` and `EXPECT_EQ(packet[5], 0x00U)`

**`PayloadContainsDroneIdBytes`**
- After id_len prefix, bytes at `packet[6]` and `packet[7]` should be `'D'` (0x44) and
  `'1'` (0x31)

**`LatitudeEncodedAsLittleEndianDouble`**
- `drone_id="D1"` (2 bytes), so latitude starts at `packet[4 + 2 + 2]` = `packet[8]`
- `memcpy` 8 bytes into a `double`, compare to `1.0`
- This verifies IEEE 754 LE encoding

**`TimestampEncodedAsLittleEndianUint64`**
- Timestamp starts at `packet[4 + 44 - 8]` = `packet[44]`
  (4 header+length + payload offset: 2+2+8+8+8+8 = 36, so `packet[4+36]` = `packet[40]`)
- More precisely: `packet[4 + 2 + id_len + 8*4]` = `packet[4+2+2+32]` = `packet[40]`
- `memcpy` 8 bytes into `uint64_t`, compare to `1000U`
- Note: `packet[40]` = 0xE8, `packet[41]` = 0x03, rest = 0x00

**`CrcAtEndMatchesCrc16OverFullFrame`**
- Last 2 bytes of packet are CRC (little-endian)
- Compute `crc16(std::span(packet.data(), packet.size() - 2))` independently
- Read CRC from packet: `uint16_t received_crc; memcpy(&received_crc, packet.data() + packet.size() - 2, 2)`
- `EXPECT_EQ(received_crc, computed_crc)`
- For `makeSimpleTelemetry()`: expected CRC = `0x5CAA`

**`TotalSizeIs6PlusPayloadSize`**
- For `drone_id="D1"` (id_len=2): total = 2 (header) + 2 (length) + 44 (payload) + 2 (CRC) = 50
- `EXPECT_EQ(packet.size(), 50U)`

**RED verification:** `cmake --build --preset=dev && ctest --preset=dev`
All `PacketSerializerTest.*` tests fail (stub returns `{}`). Zero build warnings/errors.

---

## Step 9 — 3b: PacketSerializer Implementation (GREEN)

**Files modified:**
- `protocol/src/packet_serializer.cpp` (replace stub with real implementation)

**Implementation approach:** `memcpy`-based little-endian serialization. On x86-64 Linux
(little-endian), `memcpy` of native types directly produces little-endian bytes with no
byte-swap needed (ADR-008 rationale).

**Implementation structure:**

```cpp
#include "packet_serializer.hpp"

#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include "crc16.hpp"
#include "telemetry.hpp"

namespace {

constexpr uint8_t HeaderByte0 = 0xAAU;
constexpr uint8_t HeaderByte1 = 0x55U;

void appendU16Le(std::vector<uint8_t>& buf, uint16_t val) {
    buf.push_back(static_cast<uint8_t>(val & 0xFFU));
    buf.push_back(static_cast<uint8_t>((val >> 8U) & 0xFFU));
}

void appendDouble(std::vector<uint8_t>& buf, double val) {
    std::array<uint8_t, sizeof(double)> bytes{};
    std::memcpy(bytes.data(), &val, sizeof(double));
    buf.insert(buf.end(), bytes.begin(), bytes.end());
}

void appendU64Le(std::vector<uint8_t>& buf, uint64_t val) {
    std::array<uint8_t, sizeof(uint64_t)> bytes{};
    std::memcpy(bytes.data(), &val, sizeof(uint64_t));
    buf.insert(buf.end(), bytes.begin(), bytes.end());
}

} // namespace

std::vector<uint8_t> PacketSerializer::serialize(const Telemetry& tel) {
    // Build payload
    std::vector<uint8_t> payload;
    payload.reserve(2U + tel.drone_id.size() + 8U * 4U + 8U);

    appendU16Le(payload, static_cast<uint16_t>(tel.drone_id.size()));
    payload.insert(payload.end(),
                   reinterpret_cast<const uint8_t*>(tel.drone_id.data()),
                   reinterpret_cast<const uint8_t*>(tel.drone_id.data()) +
                       tel.drone_id.size());
    appendDouble(payload, tel.latitude);
    appendDouble(payload, tel.longitude);
    appendDouble(payload, tel.altitude);
    appendDouble(payload, tel.speed);
    appendU64Le(payload, tel.timestamp);

    // Build frame: header + length + payload
    std::vector<uint8_t> packet;
    packet.reserve(2U + 2U + payload.size() + 2U);
    packet.push_back(HeaderByte0);
    packet.push_back(HeaderByte1);
    appendU16Le(packet, static_cast<uint16_t>(payload.size()));
    packet.insert(packet.end(), payload.begin(), payload.end());

    // Compute and append CRC over entire frame so far
    uint16_t const Crc = crc16(std::span<const uint8_t>(packet));
    appendU16Le(packet, Crc);

    return packet;
}
```

Key points:
- `reserve()` avoids reallocations
- `reinterpret_cast` for string bytes is correct — string data is `char`, cast to
  `uint8_t*` for `insert`. This is clang-tidy safe with `cppcoreguidelines-pro-type-reinterpret-cast`
  potentially flagged — use `static_cast<uint8_t>` in a loop as alternative if tidy
  complains
- CRC computed over the packet buffer before CRC bytes are appended
- Helper functions in anonymous namespace — not exposed in header

**GREEN verification:** `cmake --build --preset=dev && ctest --preset=dev`
All `PacketSerializerTest.*` and all `Crc16Test.*` must pass. Zero warnings.

---

## Step 10 — 3c: StreamParser Header

**Files created:**
- `protocol/include/stream_parser.hpp`

**Content requirements:**

```cpp
#ifndef STREAM_PARSER_HPP
#define STREAM_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "telemetry.hpp"

class StreamParser {
public:
    // callback must not throw — feed() is noexcept; a throwing callback
    // triggers std::terminate()
    explicit StreamParser(std::function<void(Telemetry)> on_packet);

    // Appends chunk to internal buffer and runs state machine.
    // noexcept: state machine always progresses; resync on corruption.
    void feed(std::span<const uint8_t> chunk) noexcept;

    [[nodiscard]] uint64_t getCrcFailCount() const noexcept;
    [[nodiscard]] uint64_t getMalformedCount() const noexcept;

private:
    enum class State : uint8_t {
        HUNT_HEADER,
        READ_LENGTH,
        READ_PAYLOAD,
        READ_CRC,
    };

    std::function<void(Telemetry)> on_packet_;
    std::vector<uint8_t> buffer_;
    size_t read_pos_{0};
    size_t header_start_{0};
    uint16_t pending_length_{0};
    State state_{State::HUNT_HEADER};
    uint64_t crc_fail_count_{0};
    uint64_t malformed_count_{0};
};

#endif // STREAM_PARSER_HPP
```

Key points:
- `explicit` constructor — prevents implicit conversion from `std::function`
- `enum class State : uint8_t` — scoped enum with explicit underlying type
- Member variables use trailing `_` suffix convention
- `pending_length_` stored as `uint16_t` to match wire format field size
- `header_start_` tracks position of the `0xAA` byte for rewind resync
- `[[nodiscard]]` on both getters
- Callback contract documented in comment — `std::terminate()` if callback throws
- `State` enum in private section — internal implementation detail

---

## Step 11 — 3c: StreamParser Stub

**Files created:**
- `protocol/src/stream_parser.cpp`

**`protocol/src/stream_parser.cpp` stub content:**

```cpp
#include "stream_parser.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include "telemetry.hpp"

StreamParser::StreamParser(std::function<void(Telemetry)> on_packet)
    : on_packet_(std::move(on_packet)) {}

void StreamParser::feed(std::span<const uint8_t> /*chunk*/) noexcept {}

uint64_t StreamParser::getCrcFailCount() const noexcept {
    return crc_fail_count_;
}

uint64_t StreamParser::getMalformedCount() const noexcept {
    return malformed_count_;
}
```

**`protocol/CMakeLists.txt` change:** Add `src/stream_parser.cpp` to sources. Now that
`crc16.cpp`, `packet_serializer.cpp`, and `stream_parser.cpp` exist, remove
`src/stub.cpp` from the sources list — it is no longer needed.

**Build verification:** `cmake --build --preset=dev` — must compile and link cleanly.
Removing `stub.cpp` is safe because the three real `.cpp` files now provide content.

---

## Step 12 — 3c: StreamParser Tests (RED)

**Files created:**
- `tests/protocol/parser_test.cpp`

**`tests/CMakeLists.txt` change:** Add `protocol/parser_test.cpp` to the `tests`
executable source list.

**Test approach:** Use `PacketSerializer` to produce known-good byte sequences. Feed
them to `StreamParser` with a collecting callback. Assert on collected `Telemetry`
values. This is round-trip testing — it catches both serializer and parser bugs when
combined with the serializer-only tests from Step 8.

**Test file structure:**

```cpp
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "packet_serializer.hpp"
#include "stream_parser.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)
namespace {

Telemetry makeTel(const std::string& id, double alt, double speed,
                  uint64_t ts) {
    return {.drone_id  = id,
            .latitude  = 0.0,
            .longitude = 0.0,
            .altitude  = alt,
            .speed     = speed,
            .timestamp = ts};
}

std::vector<Telemetry> collectFrom(StreamParser& parser,
                                   const std::vector<uint8_t>& bytes) {
    std::vector<Telemetry> out;
    // Attach collector inline only if parser is freshly constructed — callers
    // pass the parser already configured with a collector callback.
    parser.feed(std::span(bytes));
    return out;  // results are captured via the callback closure
}

} // namespace
// NOLINTEND(readability-magic-numbers)
```

Note: each test constructs its own `StreamParser` with a capturing lambda. The helper
`makeTel()` creates compact `Telemetry` fixtures.

**9 Test specifications:**

---

**`SingleValidPacketCallsCallbackOnce`**

```
Setup:   serialize makeTel("D1", 100.0, 20.0, 1000)
Action:  feed entire packet in one call
Assert:  callback called exactly once
         results[0].drone_id == "D1"
         results[0].altitude == 100.0
         results[0].speed == 20.0
         results[0].timestamp == 1000U
```

---

**`PacketSplitAcrossTwoFeedCallsCallsCallbackOnce`**

```
Setup:   serialize makeTel("D2", 50.0, 10.0, 2000)
         half_size = packet.size() / 2
Action:  feed(span(packet.data(), half_size))     -- no callback yet
         feed(span(packet.data() + half_size, packet.size() - half_size))
Assert:  callback called exactly once after second feed
         results[0].drone_id == "D2"
```

---

**`PacketFedOneByteAtATimeCallsCallbackOnce`**

```
Setup:   serialize makeTel("D3", 0.0, 0.0, 3000)
Action:  loop: feed(span(packet.data() + i, 1)) for i in [0, packet.size())
Assert:  callback called exactly once
         results[0].drone_id == "D3"
```

---

**`TwoPacketsInOneFeedCallsCallbackTwice`**

```
Setup:   p1 = serialize makeTel("D1", 1.0, 1.0, 1000)
         p2 = serialize makeTel("D2", 2.0, 2.0, 2000)
         stream = p1 + p2  (concatenate)
Action:  feed(stream)
Assert:  callback called exactly twice
         results[0].drone_id == "D1"
         results[1].drone_id == "D2"
```

---

**`GarbageBytesBeforeValidPacketParserResyncs`**

```
Setup:   garbage = {0x00, 0x11, 0xAA, 0x00, 0xFF} (no valid 0xAA55 sequence)
         valid   = serialize makeTel("D4", 0.0, 0.0, 4000)
         stream  = garbage + valid
Action:  feed(stream)
Assert:  callback called exactly once
         results[0].drone_id == "D4"
         getCrcFailCount() == 0   (garbage does not trigger CRC paths)
         getMalformedCount() == 0
```

---

**`PacketWithBadCrcCallbackNotCalled`**

```
Setup:   valid  = serialize makeTel("D5", 0.0, 0.0, 5000)
         corrupt = valid (copy)
         corrupt[corrupt.size() - 1] ^= 0xFF  // flip last CRC byte
         corrupt[corrupt.size() - 2] ^= 0xFF  // flip first CRC byte
Action:  feed(corrupt)
Assert:  callback never called
         getCrcFailCount() == 1
```

---

**`OversizedLengthPacketCallbackNotCalledAndMalformedIncremented`**

```
Setup:   // Manually construct: 0xAA 0x55, then LENGTH=5000 LE (0x88 0x13)
         oversized = {0xAA, 0x55, 0x88, 0x13}
         // 5000 > MAX_PAYLOAD(4096) → parser rejects immediately, no bytes follow
         valid     = serialize makeTel("D6", 0.0, 0.0, 6000)
         stream    = oversized + valid
Action:  feed(stream)
Assert:  callback called exactly once (valid packet after oversized is parsed)
         results[0].drone_id == "D6"
         getMalformedCount() == 1
         getCrcFailCount() == 0
```

Note: oversized header is only 4 bytes — the parser reads 0xAA55, then reads 2 length
bytes, sees 5000 > 4096, resyncs from byte after 0xAA (offset 1), continues. The valid
packet follows and is parsed normally.

---

**`BadCrcPacketFollowedByValidPacketOnlyValidEmitted`**

```
Setup:   corrupt = serialize makeTel("D7", 0.0, 0.0, 7000) (copy)
         corrupt[corrupt.size() - 1] ^= 0xFF
         corrupt[corrupt.size() - 2] ^= 0xFF
         valid   = serialize makeTel("D8", 0.0, 0.0, 8000)
         stream  = corrupt + valid
Action:  feed(stream)
Assert:  callback called exactly once
         results[0].drone_id == "D8"
         getCrcFailCount() == 1
```

---

**`GarbageThenValidThenGarbageThenValidBothValidEmitted`**

```
Setup:   garbage1 = {0x00, 0xFF, 0xBB}
         p1       = serialize makeTel("D9", 9.0, 9.0, 9000)
         garbage2 = {0x11, 0x22, 0x33, 0xAA}  // 0xAA alone — not a valid header
         p2       = serialize makeTel("D10", 10.0, 10.0, 10000)
         stream   = garbage1 + p1 + garbage2 + p2
Action:  feed(stream)
Assert:  callback called exactly twice
         results[0].drone_id == "D9"
         results[1].drone_id == "D10"
         getCrcFailCount() == 0
         getMalformedCount() == 0
```

---

**RED verification:** `cmake --build --preset=dev && ctest --preset=dev`
All `StreamParserTest.*` fail (stub `feed()` is a no-op). All other tests still pass.

---

## Step 13 — 3c: StreamParser Implementation (GREEN)

**Files modified:**
- `protocol/src/stream_parser.cpp` (replace stub with state machine implementation)

**Additional includes needed in the `.cpp`:**

```cpp
#include "stream_parser.hpp"

#include <algorithm>
#include <cstdint>
#include <cstring>
#include <functional>
#include <span>
#include <utility>
#include <vector>

#include "crc16.hpp"
#include "telemetry.hpp"
```

**Internal constants (in anonymous namespace):**

```cpp
namespace {
constexpr uint8_t  HeaderByte0   = 0xAAU;
constexpr uint8_t  HeaderByte1   = 0x55U;
constexpr uint16_t MaxPayload    = 4096U;
constexpr size_t   HeaderSize    = 2U;
constexpr size_t   LengthSize    = 2U;
constexpr size_t   CrcSize       = 2U;
} // namespace
```

**State machine logic overview:**

The `feed()` method appends the incoming chunk to `buffer_`, then enters a `while`
loop that runs the state machine until no more progress can be made (i.e., `read_pos_`
cannot advance further with the current buffer contents).

```
HUNT_HEADER:
  Scan from read_pos_ looking for 0xAA.
  If 0xAA found at position i:
    record header_start_ = i
    If i+1 < buffer_.size() and buffer_[i+1] == 0x55:
      read_pos_ = i + 2
      state_ = READ_LENGTH
    Else if i+1 >= buffer_.size():
      read_pos_ = i  // wait for next byte
      break (insufficient data)
    Else (buffer_[i+1] != 0x55):
      read_pos_ = i + 1  // skip this 0xAA, continue scanning
  If no 0xAA found: read_pos_ = buffer_.size(), break

READ_LENGTH:
  Need 2 bytes from read_pos_.
  If buffer_.size() - read_pos_ < 2: break (insufficient data)
  Read uint16_t LE from buffer_[read_pos_] and buffer_[read_pos_+1]
  read_pos_ += 2
  If pending_length_ > MaxPayload:
    malformed_count_++
    resync()  // read_pos_ = header_start_ + 1, state_ = HUNT_HEADER
    continue
  state_ = READ_PAYLOAD

READ_PAYLOAD:
  Need pending_length_ more bytes.
  If buffer_.size() - read_pos_ < pending_length_: break (insufficient data)
  read_pos_ += pending_length_
  state_ = READ_CRC

READ_CRC:
  Need 2 bytes.
  If buffer_.size() - read_pos_ < 2: break (insufficient data)
  Read received_crc as uint16_t LE from buffer_[read_pos_] and buffer_[read_pos_+1]
  read_pos_ += 2

  // CRC covers: header(2) + length(2) + payload(pending_length_)
  // = buffer_[header_start_ .. header_start_ + 4 + pending_length_]
  size_t frame_end = header_start_ + HeaderSize + LengthSize + pending_length_
  uint16_t computed = crc16(span(buffer_.data() + header_start_,
                                  HeaderSize + LengthSize + pending_length_))

  If received_crc != computed:
    crc_fail_count_++
    resync()
    continue

  // Valid packet: deserialize payload
  Telemetry tel = deserializePayload(...)
  // Erase consumed prefix from buffer_, reset read_pos_
  buffer_.erase(buffer_.begin(), buffer_.begin() + read_pos_)
  read_pos_ = 0
  header_start_ = 0
  pending_length_ = 0
  state_ = HUNT_HEADER
  on_packet_(std::move(tel))  // invoke callback LAST (after state reset)
```

**`resync()` helper (private method or lambda):**

```
read_pos_ = header_start_ + 1
state_ = HUNT_HEADER
// Do NOT erase buffer here — bytes before header_start_+1 are already
// behind read_pos_ and can be erased later when a packet is consumed.
// Erasing mid-resync would invalidate all stored offsets.
```

**`deserializePayload()` helper:**

Payload starts at `buffer_[header_start_ + HeaderSize + LengthSize]`.

```
payload_start = header_start_ + HeaderSize + LengthSize

// Read id_len (uint16_t LE)
uint16_t id_len; memcpy(&id_len, buffer_.data() + payload_start, 2)
offset = payload_start + 2

// Read drone_id (id_len bytes)
std::string drone_id(buffer_.data() + offset,
                     buffer_.data() + offset + id_len)
offset += id_len

// Read doubles via memcpy
double lat, lon, alt, speed; memcpy each from buffer_ at offset; offset += 8 each

// Read timestamp (uint64_t LE)
uint64_t ts; memcpy(&ts, buffer_.data() + offset, 8)

return Telemetry{drone_id, lat, lon, alt, speed, ts}
```

**Buffer cleanup strategy:**
- Only erase from buffer_ front on successful packet completion (READ_CRC match)
- `header_start_` adjusts after erase: since erase removes bytes `[0, read_pos_)`, the
  new buffer starts fresh at index 0. Reset `header_start_ = 0` and `read_pos_ = 0`.
- On resync: do not erase. The bytes before `header_start_ + 1` are already consumed
  logically (behind the new `read_pos_`). They are flushed on the next successful parse.
- On completion without errors in a long stream: buffer_ grows and shrinks per packet.
  Worst case is MAX_PAYLOAD bytes retained between successful parses. Acceptable.

**GREEN verification:** `cmake --build --preset=dev && ctest --preset=dev`
All tests pass: `Crc16Test.*`, `PacketSerializerTest.*`, `StreamParserTest.*`, and all
domain tests. Zero warnings, zero clang-tidy diagnostics.

---

## Step 14 — Final CMakeLists.txt Audit

**Files modified:**
- `protocol/CMakeLists.txt` — final state check
- `tests/CMakeLists.txt` — final state check

**`protocol/CMakeLists.txt` expected final state:**

```cmake
add_library(protocol STATIC
    src/crc16.cpp
    src/packet_serializer.cpp
    src/stream_parser.cpp
)
target_include_directories(protocol PUBLIC include)
target_compile_features(protocol PUBLIC cxx_std_20)
target_link_libraries(protocol PUBLIC domain)
```

`stub.cpp` must be removed. The three real sources replace it.

**`tests/CMakeLists.txt` expected final state:**

```cmake
project(drone-stream-parserTests LANGUAGES CXX)

add_executable(tests
    domain/drone_test.cpp
    domain/process_telemetry_test.cpp
    protocol/crc16_test.cpp
    protocol/serializer_test.cpp
    protocol/parser_test.cpp
)
target_link_libraries(tests PRIVATE protocol domain GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)
target_include_directories(tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})

enable_testing()
include(GoogleTest)
gtest_discover_tests(tests)

add_folders(Test)
```

---

## Step 15 — Full Test Suite Verification

**Command:** `cmake --build --preset=dev && ctest --preset=dev`

**Expected results:**

```
[100%] Built target tests
Test project .../build/dev
    Start 1: DroneTest.NewDroneHasEmptyAlertState
    ...
    Start N: StreamParserTest.GarbageThenValidThenGarbageThenValidBothValidEmitted
100% tests passed, 0 tests failed
```

All domain tests (`DroneTest.*`, `ProcessTelemetryTest.*`) and all protocol tests
(`Crc16Test.*`, `PacketSerializerTest.*`, `StreamParserTest.*`) must pass.

Zero warnings (`-Werror` is active). Zero clang-tidy diagnostics (runs as part of build
with the dev preset).

---

## clang-tidy Pitfall Reference

These are the clang-tidy checks most likely to trigger in protocol code, based on
patterns observed in Phase 2:

| Check | Symptom | Fix |
|-------|---------|-----|
| `llvm-header-guard` | `#pragma once` rejected | Use `#ifndef` guards |
| `readability-magic-numbers` | Raw numeric literals in non-test files | Extract to `constexpr` |
| `misc-include-cleaner` | Indirect include used | Add direct `#include` |
| `cppcoreguidelines-pro-type-reinterpret-cast` | `reinterpret_cast` on string data | Use `static_cast<uint8_t>` in loop or `memcpy` |
| `readability-identifier-naming` | Wrong case | Follow naming table in Architecture Constraints |
| `clang-diagnostic-sign-compare` | Signed/unsigned compare | Use `static_cast` or `U` suffix |
| `bugprone-narrowing-conversions` | uint16_t arithmetic promoted to int | Cast back explicitly |
| `cppcoreguidelines-avoid-magic-numbers` | Same as magic-numbers | `constexpr` |
| `performance-unnecessary-value-param` | `std::function` passed by value in impl | `std::move` in constructor body |
| `hicpp-avoid-c-arrays` | C-style array for table | Use `std::array` |

---

## Acceptance Criteria

Phase 3 is complete when ALL of the following are true:

1. `cmake --build --preset=dev` exits 0 with zero warnings and zero clang-tidy errors
2. `ctest --preset=dev` exits 0 with 100% tests passing
3. These test counts are satisfied:
   - `Crc16Test.*`: 6 tests (empty, byte 0x00, byte 0x31, byte 0xFF, Hello, 123456789)
   - `PacketSerializerTest.*`: 8 tests
   - `StreamParserTest.*`: 9 tests
4. `protocol/src/stub.cpp` is deleted (or absent from `protocol/CMakeLists.txt` sources)
5. `protocol/include/.gitkeep` is deleted
6. `tests/protocol/.gitkeep` is deleted
7. The `protocol` CMake target compiles with exactly 3 source files:
   `crc16.cpp`, `packet_serializer.cpp`, `stream_parser.cpp`
8. The `tests` CMake target includes all 5 test files (2 domain + 3 protocol)
9. No file uses `#pragma once` — all use `#ifndef` include guards
10. No file contains raw magic number literals outside of `NOLINTBEGIN/NOLINTEND` blocks
11. Phase 4 (BlockingQueue) can start immediately — Protocol boundary has no dependency
    on Phase 4 or 5

---

## Implementation Order Summary

| Step | Action                              | TDD Phase | Build Expected       |
|------|-------------------------------------|-----------|----------------------|
| 1    | Delete .gitkeep placeholders        | —         | GREEN (unchanged)    |
| 2    | Create `crc16.hpp`                  | Interface | GREEN (header only)  |
| 3    | Create `crc16.cpp` stub + CMake     | Stub      | GREEN                |
| 4    | Create `crc16_test.cpp` + CMake     | RED       | GREEN build, RED tests |
| 5    | Implement `crc16.cpp`               | GREEN     | GREEN build + tests  |
| 6    | Create `packet_serializer.hpp`      | Interface | GREEN                |
| 7    | Create `packet_serializer.cpp` stub | Stub      | GREEN                |
| 8    | Create `serializer_test.cpp` + CMake| RED       | GREEN build, RED tests |
| 9    | Implement `packet_serializer.cpp`   | GREEN     | GREEN build + tests  |
| 10   | Create `stream_parser.hpp`          | Interface | GREEN                |
| 11   | Create `stream_parser.cpp` stub + remove stub.cpp | Stub | GREEN      |
| 12   | Create `parser_test.cpp` + CMake    | RED       | GREEN build, RED tests |
| 13   | Implement `stream_parser.cpp`       | GREEN     | GREEN build + tests  |
| 14   | Audit CMakeLists.txt files          | —         | GREEN                |
| 15   | Full test suite verification        | —         | 100% pass            |
