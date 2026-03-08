# Phase 6: Client Binary — Implementation Plan

**Date:** 2026-03-08
**Status:** Active
**Depends on:** Phase 3 (PacketSerializer, crc16, protocol_constants), Phase 5 (server must be running for integration tests)
**Test strategy:** TDD for PacketBuilder (unit tests in `tests/`); integration testing for scenarios (manual, client vs. live server).

---

## Overview

Phase 6 delivers the `drone_client` binary, which exercises all server-side logic
through seven named scenarios. It is also the primary integration test harness for
the full 3-stage server pipeline from Phase 5.

The phase has two sub-phases:

- **6a: PacketBuilder** — a utility class with static factory methods for constructing
  well-formed, malformed, and adversarial packets. It wraps `PacketSerializer` and
  `crc16` and is fully unit-tested because its outputs are deterministic.
- **6b: Client main.cpp** — CLI parsing, a TCP connect helper, and seven scenario
  functions that send packet streams to the server.

**Dependency constraint within phase:** 6a before 6b. `main.cpp` uses `PacketBuilder`.

---

## Current State of `client/`

```
client/
  include/          (empty — only .gitkeep)
  src/
    main.cpp        (stub: int main() { return 0; })
  CMakeLists.txt
```

### Existing `client/CMakeLists.txt`

```cmake
add_executable(drone_client src/main.cpp)
target_include_directories(drone_client PRIVATE include)
target_compile_features(drone_client PRIVATE cxx_std_20)
target_link_libraries(drone_client PRIVATE protocol domain spdlog::spdlog)
```

No changes are needed to the link line. `protocol` already provides `PacketSerializer`
and `crc16`. `domain` provides `Telemetry` and `AlertPolicy`.

---

## Key Interfaces Available to the Client

### `PacketSerializer::serialize` (`protocol/include/packet_serializer.hpp`)

```cpp
[[nodiscard]] static std::vector<uint8_t> serialize(const Telemetry& tel);
```

Returns a complete framed packet: `[0xAA 0x55][LENGTH:u16 LE][PAYLOAD][CRC:u16 LE]`.

### `crc16` (`protocol/include/crc16.hpp`)

```cpp
[[nodiscard]] uint16_t crc16(std::span<const uint8_t> data) noexcept;
```

### `protocol_constants` (`protocol/include/protocol_constants.hpp`)

```cpp
namespace protocol {
  inline constexpr uint8_t HeaderByte0 = 0xAAU;
  inline constexpr uint8_t HeaderByte1 = 0x55U;
  inline constexpr std::size_t HeaderSize = 2U;
  inline constexpr std::size_t LengthFieldSize = 2U;
  inline constexpr std::size_t CrcFieldSize = 2U;
  // ...
}
```

### `Telemetry` (`domain/include/telemetry.hpp`)

```cpp
struct Telemetry {
  std::string drone_id;
  double latitude;
  double longitude;
  double altitude;
  double speed;
  uint64_t timestamp;
};
```

### `AlertPolicy` (`domain/include/alert_policy.hpp`)

```cpp
inline constexpr double DefaultAltitudeLimit = 120.0;
inline constexpr double DefaultSpeedLimit = 50.0;

struct AlertPolicy {
  double altitude_limit = DefaultAltitudeLimit;
  double speed_limit = DefaultSpeedLimit;
};
```

---

## Wire Format Reference

```
Frame:   [0xAA 0x55] [LENGTH:u16 LE] [PAYLOAD:N bytes] [CRC:u16 LE]
Payload: [id_len:u16 LE] [drone_id bytes] [lat:f64 LE] [lon:f64 LE]
         [alt:f64 LE] [speed:f64 LE] [timestamp:u64 LE]
```

The CRC covers `header + length + payload` (i.e., everything except the trailing
CRC bytes themselves). This is exactly what `PacketSerializer::serialize` already
computes.

Minimum fixed payload overhead: `id_len(2) + 4*double(32) + timestamp(8) = 42` bytes.
Typical packet size for a short drone ID (e.g., `"D1"`, 2 bytes): `2+2+42+2 = 50` bytes.

---

## Task 6a: PacketBuilder

**Story Points:** 3

### Files

- `client/include/packet_builder.hpp` (new)
- `client/src/packet_builder.cpp` (new)
- `client/CMakeLists.txt` (add `packet_builder.cpp`)
- `tests/client/packet_builder_test.cpp` (new)
- `tests/CMakeLists.txt` (add test file and include path)

### Step 1: Write `client/include/packet_builder.hpp`

```cpp
#ifndef PACKET_BUILDER_HPP
#define PACKET_BUILDER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemetry.hpp"

/// Utility class for constructing telemetry packets in various states
/// (valid, corrupt, malformed) for use by the drone client scenarios.
///
/// All methods are static — PacketBuilder has no state.
class PacketBuilder {
public:
  /// Returns a fully valid, CRC-correct framed packet.
  [[nodiscard]] static std::vector<uint8_t> validPacket(const Telemetry& tel);

  /// Returns a valid packet with the last 2 bytes (CRC field) XORed with 0xFF,
  /// producing a guaranteed CRC mismatch.
  [[nodiscard]] static std::vector<uint8_t> corruptCrc(const Telemetry& tel);

  /// Returns `count` pseudo-random bytes that do not begin with the header
  /// sequence [0xAA, 0x55]. The server parser must skip these via resync.
  [[nodiscard]] static std::vector<uint8_t> garbageBytes(std::size_t count);

  /// Returns a 4-byte frame with header [0xAA, 0x55] and LENGTH=5000 (0x1388)
  /// little-endian, which exceeds the parser's MAX_PAYLOAD (4096).
  /// The server increments its malformed counter and resyncs.
  [[nodiscard]] static std::vector<uint8_t> oversizeLength();

  /// Splits `packet` into chunks of at most `chunk_size` bytes.
  /// The caller sends each chunk as a separate TCP send() call.
  [[nodiscard]] static std::vector<std::vector<uint8_t>>
  fragment(std::vector<uint8_t> packet, std::size_t chunk_size);
};

#endif // PACKET_BUILDER_HPP
```

Design notes:
- All methods are `[[nodiscard]]` and `static`. No constructor, destructor, or
  member variables. This class is a pure namespace substitute with type-safety.
- `fragment` returns `std::vector<std::vector<uint8_t>>` — each inner vector is
  a TCP send unit. The original master plan signature `fragment(packet, chunk_size)`
  was ambiguous about the return type; the caller iterates the returned chunks and
  calls `send()` on each. This is cleaner than returning a joined vector.
- `garbageBytes` must not accidentally produce `[0xAA, 0x55]` at the start (the
  server would treat that as a header). See implementation note below.

### Step 2: Write stub `client/src/packet_builder.cpp`

```cpp
#include "packet_builder.hpp"

#include <vector>
#include "telemetry.hpp"

std::vector<uint8_t> PacketBuilder::validPacket(const Telemetry&) {
  return {};
}

std::vector<uint8_t> PacketBuilder::corruptCrc(const Telemetry&) {
  return {};
}

std::vector<uint8_t> PacketBuilder::garbageBytes(std::size_t) {
  return {};
}

std::vector<uint8_t> PacketBuilder::oversizeLength() {
  return {};
}

std::vector<std::vector<uint8_t>> PacketBuilder::fragment(
    std::vector<uint8_t>, std::size_t) {
  return {};
}
```

### Step 3: Update `client/CMakeLists.txt`

```cmake
add_executable(drone_client
  src/packet_builder.cpp
  src/main.cpp
)
target_include_directories(drone_client PRIVATE include)
target_compile_features(drone_client PRIVATE cxx_std_20)
target_link_libraries(drone_client PRIVATE protocol domain spdlog::spdlog)
```

### Step 4: Write RED tests in `tests/client/packet_builder_test.cpp`

Create directory `tests/client/` (no `.gitkeep` needed once the test file exists).

```cpp
#include <cstdint>
#include <cstring>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"
#include "packet_builder.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

auto makeTel(const std::string& id) -> Telemetry {
  return {.drone_id = id,
          .latitude = 1.0,
          .longitude = 2.0,
          .altitude = 50.0,
          .speed = 10.0,
          .timestamp = 1000U};
}

} // namespace

// --- validPacket ---

TEST(PacketBuilderTest, ValidPacketStartsWithHeader) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketBuilderTest, ValidPacketCrcIsCorrect) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  ASSERT_GE(packet.size(), 4U);
  auto frame = std::span<const uint8_t>(packet.data(), packet.size() - 2);
  uint16_t expected_crc = crc16(frame);
  uint16_t actual_crc = 0;
  std::memcpy(&actual_crc, &packet[packet.size() - 2], sizeof(actual_crc));
  EXPECT_EQ(actual_crc, expected_crc);
}

TEST(PacketBuilderTest, ValidPacketIsNonEmpty) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  EXPECT_FALSE(packet.empty());
}

// --- corruptCrc ---

TEST(PacketBuilderTest, CorruptCrcStartsWithHeader) {
  auto packet = PacketBuilder::corruptCrc(makeTel("D2"));
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketBuilderTest, CorruptCrcHasWrongCrc) {
  auto packet = PacketBuilder::corruptCrc(makeTel("D2"));
  ASSERT_GE(packet.size(), 4U);
  auto frame = std::span<const uint8_t>(packet.data(), packet.size() - 2);
  uint16_t expected_crc = crc16(frame);
  uint16_t actual_crc = 0;
  std::memcpy(&actual_crc, &packet[packet.size() - 2], sizeof(actual_crc));
  EXPECT_NE(actual_crc, expected_crc);
}

TEST(PacketBuilderTest, CorruptCrcHasSameSizeAsValidPacket) {
  auto valid = PacketBuilder::validPacket(makeTel("D2"));
  auto corrupt = PacketBuilder::corruptCrc(makeTel("D2"));
  EXPECT_EQ(valid.size(), corrupt.size());
}

// --- garbageBytes ---

TEST(PacketBuilderTest, GarbageBytesHasCorrectSize) {
  auto garbage = PacketBuilder::garbageBytes(32);
  EXPECT_EQ(garbage.size(), 32U);
}

TEST(PacketBuilderTest, GarbageBytesDoesNotStartWithHeader) {
  // Run many times to catch probabilistic header byte collisions
  for (int i = 0; i < 100; ++i) {
    auto garbage = PacketBuilder::garbageBytes(16);
    ASSERT_GE(garbage.size(), 2U);
    // Must not begin with 0xAA 0x55 — the server would treat it as a header
    bool is_header = (garbage[0] == 0xAAU && garbage[1] == 0x55U);
    EXPECT_FALSE(is_header) << "garbageBytes produced a valid header prefix";
  }
}

TEST(PacketBuilderTest, GarbageBytesZeroCountReturnsEmpty) {
  auto garbage = PacketBuilder::garbageBytes(0);
  EXPECT_TRUE(garbage.empty());
}

// --- oversizeLength ---

TEST(PacketBuilderTest, OversizeLengthStartsWithHeader) {
  auto packet = PacketBuilder::oversizeLength();
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketBuilderTest, OversizeLengthFieldIs5000) {
  auto packet = PacketBuilder::oversizeLength();
  ASSERT_GE(packet.size(), 4U);
  uint16_t length = 0;
  std::memcpy(&length, &packet[2], sizeof(length));
  EXPECT_EQ(length, 5000U);
}

TEST(PacketBuilderTest, OversizeLengthIs4Bytes) {
  // Only header + length field — no payload or CRC follows
  auto packet = PacketBuilder::oversizeLength();
  EXPECT_EQ(packet.size(), 4U);
}

// --- fragment ---

TEST(PacketBuilderTest, FragmentReassemblesCorrectly) {
  auto packet = PacketBuilder::validPacket(makeTel("D3"));
  auto chunks = PacketBuilder::fragment(packet, 3);

  std::vector<uint8_t> reassembled;
  for (const auto& chunk : chunks) {
    reassembled.insert(reassembled.end(), chunk.begin(), chunk.end());
  }
  EXPECT_EQ(reassembled, packet);
}

TEST(PacketBuilderTest, FragmentChunkSizeIsRespected) {
  auto packet = PacketBuilder::validPacket(makeTel("D3"));
  auto chunks = PacketBuilder::fragment(packet, 3);
  for (std::size_t i = 0; i + 1 < chunks.size(); ++i) {
    EXPECT_EQ(chunks[i].size(), 3U);
  }
  // Last chunk may be smaller
  EXPECT_LE(chunks.back().size(), 3U);
}

TEST(PacketBuilderTest, FragmentWithChunkSizeLargerThanPacketReturnsSingleChunk) {
  auto packet = PacketBuilder::validPacket(makeTel("D3"));
  auto chunks = PacketBuilder::fragment(packet, 1024);
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks[0], packet);
}

TEST(PacketBuilderTest, FragmentWith1ByteChunksProducesOneChunkPerByte) {
  auto packet = PacketBuilder::validPacket(makeTel("D3"));
  auto chunks = PacketBuilder::fragment(packet, 1);
  EXPECT_EQ(chunks.size(), packet.size());
}

// NOLINTEND(readability-magic-numbers)
```

### Step 5: Update `tests/CMakeLists.txt`

Add the new test file and include path so the test target can find `packet_builder.hpp`:

```cmake
add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
  protocol/crc16_test.cpp
  protocol/serializer_test.cpp
  protocol/parser_test.cpp
  common/blocking_queue_test.cpp
  client/packet_builder_test.cpp        # NEW
)
target_link_libraries(tests PRIVATE protocol domain common GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)
target_include_directories(tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/client/include  # NEW — for packet_builder.hpp
)
```

Note: the `tests` target does not link `drone_client`'s object files. The
`packet_builder.cpp` source must be compiled separately for the test target. There are
two options:

**Option A (preferred): Add `packet_builder.cpp` as a source to the `tests` target.**

This avoids creating a separate static library for a single utility file.

```cmake
add_executable(tests
  ...
  client/packet_builder_test.cpp
  ${PROJECT_SOURCE_DIR}/client/src/packet_builder.cpp  # compile directly
)
```

**Option B: Extract `PacketBuilder` into a separate static library.**

Only warranted if the builder is used from more than two places. For this project,
it is used from `drone_client` and `tests` — Option A is simpler.

Use Option A. The final `tests/CMakeLists.txt` with Option A:

```cmake
project(drone-stream-parserTests LANGUAGES CXX)

add_executable(tests
  domain/drone_test.cpp
  domain/process_telemetry_test.cpp
  protocol/crc16_test.cpp
  protocol/serializer_test.cpp
  protocol/parser_test.cpp
  common/blocking_queue_test.cpp
  client/packet_builder_test.cpp
  ${PROJECT_SOURCE_DIR}/client/src/packet_builder.cpp
)
target_link_libraries(tests PRIVATE protocol domain common GTest::gtest_main GTest::gmock)
target_compile_features(tests PRIVATE cxx_std_20)
target_include_directories(tests PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${PROJECT_SOURCE_DIR}/client/include
)

enable_testing()
include(GoogleTest)
gtest_discover_tests(tests)

add_folders(Test)
```

### Step 6: Verify RED (build succeeds, tests fail)

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds (stub returns empty vectors). Tests that check size/content
fail (RED). The `ValidPacketIsNonEmpty` test fails immediately.

### Step 7: Implement `client/src/packet_builder.cpp` (GREEN)

Full implementation:

```cpp
#include "packet_builder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "crc16.hpp"
#include "packet_serializer.hpp"
#include "protocol_constants.hpp"
#include "telemetry.hpp"

namespace {

// A simple LCG for deterministic pseudo-random garbage bytes.
// Not cryptographic — we only need bytes that avoid the header sequence.
class ByteGen {
public:
  explicit ByteGen(uint64_t seed) : state_(seed) {}

  auto next() -> uint8_t {
    // LCG parameters from Knuth (MMIX)
    constexpr uint64_t A = 6364136223846793005ULL;
    constexpr uint64_t C = 1442695040888963407ULL;
    state_ = A * state_ + C;
    return static_cast<uint8_t>(state_ >> 56U);
  }

private:
  uint64_t state_;
};

} // namespace

std::vector<uint8_t> PacketBuilder::validPacket(const Telemetry& tel) {
  return PacketSerializer::serialize(tel);
}

std::vector<uint8_t> PacketBuilder::corruptCrc(const Telemetry& tel) {
  auto packet = PacketSerializer::serialize(tel);
  // XOR both CRC bytes — guaranteed to produce a non-zero change and thus
  // a CRC mismatch (since XORing with 0xFF flips all bits).
  packet[packet.size() - 1] ^= 0xFFU;
  packet[packet.size() - 2] ^= 0xFFU;
  return packet;
}

std::vector<uint8_t> PacketBuilder::garbageBytes(std::size_t count) {
  if (count == 0) {
    return {};
  }

  // Seed with a fixed value for reproducibility in tests.
  ByteGen gen{0xDEADBEEFCAFEBABEULL};

  std::vector<uint8_t> result;
  result.reserve(count);

  // Generate first byte: must not be 0xAA (to avoid header prefix at position 0).
  uint8_t first = gen.next();
  while (first == protocol::HeaderByte0) {
    first = gen.next();
  }
  result.push_back(first);

  // Generate remaining bytes freely.
  for (std::size_t i = 1; i < count; ++i) {
    result.push_back(gen.next());
  }

  return result;
}

std::vector<uint8_t> PacketBuilder::oversizeLength() {
  // Header + LENGTH=5000 (0x1388) LE.
  // No payload or CRC — the server rejects on the oversized length field alone.
  constexpr uint16_t OversizeLengthValue = 5000U;
  std::vector<uint8_t> packet;
  packet.reserve(protocol::HeaderSize + protocol::LengthFieldSize);
  packet.push_back(protocol::HeaderByte0);
  packet.push_back(protocol::HeaderByte1);
  // Little-endian encoding of OversizeLengthValue
  packet.push_back(static_cast<uint8_t>(OversizeLengthValue & 0xFFU));
  packet.push_back(static_cast<uint8_t>((OversizeLengthValue >> 8U) & 0xFFU));
  return packet;
}

std::vector<std::vector<uint8_t>> PacketBuilder::fragment(
    std::vector<uint8_t> packet, std::size_t chunk_size) {
  std::vector<std::vector<uint8_t>> chunks;
  if (packet.empty() || chunk_size == 0) {
    return chunks;
  }

  for (std::size_t offset = 0; offset < packet.size(); offset += chunk_size) {
    auto begin = packet.begin() + static_cast<ptrdiff_t>(offset);
    auto end = (offset + chunk_size <= packet.size())
                   ? begin + static_cast<ptrdiff_t>(chunk_size)
                   : packet.end();
    chunks.emplace_back(begin, end);
  }
  return chunks;
}
```

Implementation notes:

- `garbageBytes` uses a fixed-seed LCG so tests are deterministic. The server
  parser is exercised with consistent byte patterns across runs.
- Only the first byte of garbage is constrained (must not be `0xAA`). The
  probability of accidentally producing `0xAA 0x55` at offset 0 is 1/256 per
  call without the first-byte check. The check makes it deterministically safe.
- `oversizeLength` produces exactly 4 bytes: the parser reads the header, reads
  LENGTH=5000, rejects it as > MAX_PAYLOAD (4096), increments `malformed_count_`,
  and resyncs. The server is not sent any additional payload bytes.
- `corruptCrc` XORs with `0xFF` (not `0x01`) — flipping all bits ensures the
  result differs from the original regardless of the original CRC value (the only
  value that XOR with `0xFF` leaves unchanged is `0xFF` itself for each byte, but
  since both bytes are XORed, the 16-bit CRC cannot stay the same).

### Step 8: Verify GREEN

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: all existing tests pass, all `PacketBuilderTest` tests pass.

---

## Task 6b: Client main.cpp

**Story Points:** 8

### Files

- `client/src/main.cpp` (replace stub with full implementation)
- No new headers needed (all logic is in `main.cpp` with internal helpers)
- No changes to `client/CMakeLists.txt` (already lists `main.cpp`)

### 6b.1: Structure Overview

`main.cpp` contains:

1. `parseArgs()` — parses `--scenario`, `--host`, `--port` from argv
2. `tcpConnect()` — POSIX socket + connect, returns fd or throws
3. `sendAll()` — wraps `send()` in a loop to handle partial sends
4. Seven scenario functions: each takes `(int fd)` and sends packets
5. `main()` — dispatches to the correct scenario

All helpers are in the anonymous namespace. There is no separate header; the
client binary has no public API.

### 6b.2: Includes

```cpp
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <functional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include <spdlog/spdlog.h>

#include "alert_policy.hpp"
#include "packet_builder.hpp"
#include "telemetry.hpp"
```

### 6b.3: `parseArgs`

```cpp
struct Args {
  std::string scenario;
  std::string host{"127.0.0.1"};
  uint16_t port{9000};
};

// In anonymous namespace:
auto parseArgs(int argc, char** argv) -> Args {
  Args args;
  for (int i = 1; i < argc; ++i) {
    std::string arg{argv[i]};  // NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    if (arg == "--scenario" && i + 1 < argc) {
      args.scenario = argv[++i]; // NOLINT
    } else if (arg == "--host" && i + 1 < argc) {
      args.host = argv[++i]; // NOLINT
    } else if (arg == "--port" && i + 1 < argc) {
      args.port = static_cast<uint16_t>(std::stoi(argv[++i])); // NOLINT
    }
  }
  return args;
}
```

Clang-tidy note: `argv` pointer arithmetic triggers
`cppcoreguidelines-pro-bounds-pointer-arithmetic`. Suppress with `// NOLINT`
inline on affected lines — this is the standard CLI parsing pattern in C++.

### 6b.4: `tcpConnect`

```cpp
auto tcpConnect(const std::string& host, uint16_t port) -> int {
  const int Fd = socket(AF_INET, SOCK_STREAM, 0);
  if (Fd < 0) {
    throw std::runtime_error(
        std::string("socket() failed: ") +
        std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);

  if (inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    close(Fd);
    throw std::runtime_error("Invalid host address: " + host);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (connect(Fd, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    auto msg = std::string("connect() failed: ") +
               std::strerror(errno); // NOLINT(concurrency-mt-unsafe)
    close(Fd);
    throw std::runtime_error(msg);
  }

  return Fd;
}
```

### 6b.5: `sendAll`

TCP `send()` may return fewer bytes than requested if the kernel buffer is full.
`sendAll` ensures every byte is sent:

```cpp
void sendAll(int fd, const std::vector<uint8_t>& data) {
  std::size_t total = 0;
  while (total < data.size()) {
    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    const ssize_t Sent = send(fd, data.data() + total, data.size() - total, 0);
    if (Sent < 0) {
      throw std::runtime_error(
          std::string("send() failed: ") +
          std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
    }
    total += static_cast<std::size_t>(Sent);
  }
}
```

Clang-tidy note: `data.data() + total` triggers pointer arithmetic warning.
Suppress with `// NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)` or use
a span:

```cpp
auto span = std::span<const uint8_t>(data).subspan(total);
const ssize_t Sent = send(fd, span.data(), span.size(), 0);
```

Use the span form — it avoids the suppression and is more idiomatic.

### 6b.6: Telemetry Factory Helpers

These two helpers are used across multiple scenarios:

```cpp
auto makeTel(const std::string& id, double alt, double speed,
             uint64_t ts) -> Telemetry {
  return {.drone_id = id,
          .latitude = 0.0,
          .longitude = 0.0,
          .altitude = alt,
          .speed = speed,
          .timestamp = ts};
}

auto makeTelNormal(const std::string& id, uint64_t ts) -> Telemetry {
  return makeTel(id, 50.0, 20.0, ts);  // below both thresholds
}
```

Use `uint64_t` for the timestamp parameter. Monotonically incrementing from 0 or
using a counter is fine — the server does not validate timestamps.

### 6b.7: Scenario 1 — `normal`

**Goal:** 1000 valid packets, 5 drone IDs cycling, 1ms delay.

```
Server expects: 1000 packets, 5 unique drones, 0 CRC failures, 0 malformed.
```

```cpp
void runNormal(int fd) {
  constexpr int TotalPackets = 1000;
  constexpr int NumDrones = 5;
  constexpr auto Delay = std::chrono::milliseconds(1);

  const std::array<std::string, NumDrones> ids{
      "drone-0", "drone-1", "drone-2", "drone-3", "drone-4"};

  spdlog::info("normal: sending {} valid packets across {} drones", TotalPackets, NumDrones);

  for (int i = 0; i < TotalPackets; ++i) {
    const auto& id = ids[static_cast<std::size_t>(i) % NumDrones];
    auto packet = PacketBuilder::validPacket(
        makeTelNormal(id, static_cast<uint64_t>(i)));
    sendAll(fd, packet);
    std::this_thread::sleep_for(Delay);
  }

  spdlog::info("normal: done");
}
```

### 6b.8: Scenario 2 — `fragmented`

**Goal:** Same 1000 packets, each fragmented into 1–3 byte TCP sends.

The fragment size alternates between 1, 2, and 3 bytes using modular cycling.
This exercises the parser's byte-at-a-time reassembly path.

```cpp
void runFragmented(int fd) {
  constexpr int TotalPackets = 1000;
  constexpr int NumDrones = 5;

  const std::array<std::string, NumDrones> ids{
      "drone-0", "drone-1", "drone-2", "drone-3", "drone-4"};

  spdlog::info("fragmented: sending {} packets in 1-3 byte fragments", TotalPackets);

  for (int i = 0; i < TotalPackets; ++i) {
    const auto& id = ids[static_cast<std::size_t>(i) % NumDrones];
    auto packet = PacketBuilder::validPacket(
        makeTelNormal(id, static_cast<uint64_t>(i)));

    // Cycle through fragment sizes 1, 2, 3
    const std::size_t ChunkSize = (static_cast<std::size_t>(i) % 3) + 1;
    auto chunks = PacketBuilder::fragment(std::move(packet), ChunkSize);

    for (const auto& chunk : chunks) {
      sendAll(fd, chunk);
    }
  }

  spdlog::info("fragmented: done");
}
```

No `sleep_for` here — the real-world fragmentation stress comes from tiny sends,
not timing. The kernel may Nagle-coalesce adjacent sends; that is fine. The parser
must handle whatever boundaries `recv()` produces on the server side.

### 6b.9: Scenario 3 — `corrupt`

**Goal:** Interleaved stream: 30% garbage bytes, 20% bad-CRC packets, 50% valid.

The distribution is implemented deterministically using modulo arithmetic on the
packet index so results are reproducible:

- `i % 10 < 3` (indices 0,1,2): garbage bytes (32 bytes)
- `i % 10 < 5` (indices 3,4): corrupt CRC
- else (indices 5–9): valid packet

100 iterations is sufficient to exercise all paths:

```cpp
void runCorrupt(int fd) {
  constexpr int TotalIterations = 100;

  spdlog::info("corrupt: sending {} iterations (30% garbage, 20% bad-CRC, 50% valid)",
               TotalIterations);

  for (int i = 0; i < TotalIterations; ++i) {
    auto id = "corrupt-drone-" + std::to_string(i % 5);
    const auto Ts = static_cast<uint64_t>(i);

    const int Bucket = i % 10;
    if (Bucket < 3) {  // NOLINT(readability-magic-numbers)
      // 30%: garbage bytes
      sendAll(fd, PacketBuilder::garbageBytes(32));
    } else if (Bucket < 5) {  // NOLINT(readability-magic-numbers)
      // 20%: bad CRC
      sendAll(fd, PacketBuilder::corruptCrc(makeTelNormal(id, Ts)));
    } else {
      // 50%: valid
      sendAll(fd, PacketBuilder::validPacket(makeTelNormal(id, Ts)));
    }
  }

  spdlog::info("corrupt: done");
}
```

Clang-tidy note: the magic numbers 3 and 5 have clear semantic meaning from the
comment. Suppress with `// NOLINT(readability-magic-numbers)` inline, or define
named constants:

```cpp
constexpr int GarbageThreshold = 3;
constexpr int CorruptThreshold = 5;
```

Use named constants — they self-document the distribution.

### 6b.10: Scenario 4 — `stress`

**Goal:** Maximum rate valid packets for 10 seconds. Log send rate at end.

```cpp
void runStress(int fd) {
  constexpr auto Duration = std::chrono::seconds(10);
  spdlog::info("stress: sending at maximum rate for {} seconds", Duration.count());

  const auto Start = std::chrono::steady_clock::now();
  uint64_t count = 0;

  while (std::chrono::steady_clock::now() - Start < Duration) {
    auto packet = PacketBuilder::validPacket(
        makeTelNormal("stress-drone", count));
    sendAll(fd, packet);
    ++count;
  }

  const double ElapsedSec = static_cast<double>(Duration.count());
  const double Rate = static_cast<double>(count) / ElapsedSec;
  spdlog::info("stress: sent {} packets in {}s ({:.0f} pkt/s)", count,
               Duration.count(), Rate);
}
```

Target: >= 1000 pkt/s (as per master plan). With a ~50-byte packet and loopback
TCP, this is easily achievable. The bottleneck will be the server's processing
pipeline, not the client.

### 6b.11: Scenario 5 — `alert`

**Goal:** Send packets with altitude=150.0 (> 120.0 limit) and speed=60.0
(> 50.0 limit) for 3 drones. Server must log ALTITUDE and SPEED alerts.

```cpp
void runAlert(int fd) {
  constexpr double AlertAltitude = 150.0;  // above DefaultAltitudeLimit (120.0)
  constexpr double AlertSpeed = 60.0;      // above DefaultSpeedLimit (50.0)
  constexpr int PacketsPerDrone = 5;
  constexpr int NumDrones = 3;

  spdlog::info("alert: sending over-threshold packets for {} drones "
               "(alt={:.1f}, speed={:.1f})", NumDrones, AlertAltitude, AlertSpeed);

  for (int drone = 0; drone < NumDrones; ++drone) {
    auto id = "alert-drone-" + std::to_string(drone);
    for (int i = 0; i < PacketsPerDrone; ++i) {
      auto tel = makeTel(id, AlertAltitude, AlertSpeed,
                         static_cast<uint64_t>(drone * PacketsPerDrone + i));
      sendAll(fd, PacketBuilder::validPacket(tel));
    }
  }

  spdlog::info("alert: done — check server log for ALTITUDE and SPEED alerts");
}
```

Only one ALTITUDE and one SPEED alert is emitted per drone per alert state
transition (enter). Sending 5 packets per drone with constant above-threshold
values means: 1 ALTITUDE ENTERED + 1 SPEED ENTERED per drone = 6 total alerts
across 3 drones.

### 6b.12: Scenario 6 — `multi-drone`

**Goal:** 100 unique drone IDs, 10 packets each = 1000 total packets.

```cpp
void runMultiDrone(int fd) {
  constexpr int NumDrones = 100;
  constexpr int PacketsPerDrone = 10;

  spdlog::info("multi-drone: {} unique drones, {} packets each",
               NumDrones, PacketsPerDrone);

  for (int drone = 0; drone < NumDrones; ++drone) {
    auto id = "multi-" + std::to_string(drone);
    for (int i = 0; i < PacketsPerDrone; ++i) {
      auto ts = static_cast<uint64_t>(drone * PacketsPerDrone + i);
      sendAll(fd, PacketBuilder::validPacket(makeTelNormal(id, ts)));
    }
  }

  spdlog::info("multi-drone: done — server should report {} active drones", NumDrones);
}
```

### 6b.13: Scenario 7 — `interleaved`

**Goal:** 5 drones, packets sent round-robin. Per-drone state must not leak
between drones (each drone's alert state is independent in `InMemoryDroneRepository`).

```cpp
void runInterleaved(int fd) {
  constexpr int NumDrones = 5;
  constexpr int RoundsPerDrone = 50;  // 250 total packets
  constexpr double NormalAlt = 50.0;
  constexpr double NormalSpeed = 20.0;

  spdlog::info("interleaved: {} drones round-robin, {} rounds each",
               NumDrones, RoundsPerDrone);

  const std::array<std::string, NumDrones> ids{
      "inter-0", "inter-1", "inter-2", "inter-3", "inter-4"};

  for (int round = 0; round < RoundsPerDrone; ++round) {
    for (int drone = 0; drone < NumDrones; ++drone) {
      auto ts = static_cast<uint64_t>(round * NumDrones + drone);
      auto tel = makeTel(ids[static_cast<std::size_t>(drone)],
                         NormalAlt, NormalSpeed, ts);
      sendAll(fd, PacketBuilder::validPacket(tel));
    }
  }

  spdlog::info("interleaved: done — server should show {} drones, no state cross-contamination",
               NumDrones);
}
```

### 6b.14: Dispatch Table and `main()`

Use a map from scenario name to function to keep `main()` clean:

```cpp
auto main(int argc, char** argv) -> int {
  const auto Args = parseArgs(argc, argv);

  if (Args.scenario.empty()) {
    spdlog::error("Usage: drone_client --scenario <name> [--host <addr>] [--port <port>]");
    spdlog::error("Scenarios: normal, fragmented, corrupt, stress, alert, multi-drone, interleaved");
    return 1;
  }

  const std::unordered_map<std::string, std::function<void(int)>> Scenarios{
      {"normal",      runNormal},
      {"fragmented",  runFragmented},
      {"corrupt",     runCorrupt},
      {"stress",      runStress},
      {"alert",       runAlert},
      {"multi-drone", runMultiDrone},
      {"interleaved", runInterleaved},
  };

  auto it = Scenarios.find(Args.scenario);
  if (it == Scenarios.end()) {
    spdlog::error("Unknown scenario: '{}'. Valid: normal, fragmented, corrupt, stress, "
                  "alert, multi-drone, interleaved", Args.scenario);
    return 1;
  }

  spdlog::info("Connecting to {}:{}", Args.host, Args.port);
  const int Fd = tcpConnect(Args.host, Args.port);
  spdlog::info("Connected");

  try {
    it->second(Fd);
  } catch (const std::exception& ex) {
    spdlog::error("Scenario failed: {}", ex.what());
    close(Fd);
    return 1;
  }

  close(Fd);
  spdlog::info("Disconnected");
  return 0;
}
```

Clang-tidy note: `std::unordered_map` with `std::function` may trigger
`performance-unnecessary-value-param` or similar. The map is constructed once and
only `find()` is called on it — this is not a hot path. If clang-tidy warns,
consider a flat array of `{name, fn}` pairs or add `// NOLINT`.

### Step 9: Verify client builds

```bash
cmake --build --preset=dev && ctest --preset=dev
```

Expected: build succeeds, all existing tests green. The `drone_client` binary is
at `./build/dev/drone_client`.

---

## Integration Testing — All 7 Scenarios

Run these in two terminals. The server must be started before the client connects.
The server shuts down after the client disconnects (because TcpServer returns from
`recvLoop` on EOF and then `stop_flag_` is checked on the next poll timeout — the
server does NOT shut down automatically). Use Ctrl-C to stop the server between
scenarios, or keep it running and reconnect multiple clients.

**Note:** The server accepts one client at a time (sequential accept loop in Phase
5). Each scenario connects, runs, disconnects, and the server returns to `accept()`.
The server continues running unless stopped with Ctrl-C.

### Terminal 1: Start server

```bash
./build/dev/drone_server --port 9000
```

Leave running for all scenarios. Stop with Ctrl-C after all scenarios complete.

### Scenario 1: `normal`

```bash
./build/dev/drone_client --scenario normal --host 127.0.0.1 --port 9000
```

Expected server output (approximate):
```
[info] TcpServer: client connected from 127.0.0.1
[info] TcpServer: client disconnected
```

Expected shutdown summary (after Ctrl-C):
```
[info] Shutdown complete. packets_processed=1000 crc_failures=0 malformed=0 active_drones=5
```

Verification checklist:
- `packets_processed=1000`
- `crc_failures=0`
- `malformed=0`
- `active_drones=5`

### Scenario 2: `fragmented`

Restart server (Ctrl-C then restart), then:

```bash
./build/dev/drone_client --scenario fragmented --host 127.0.0.1 --port 9000
```

Expected shutdown summary:
```
packets_processed=1000  crc_failures=0  malformed=0  active_drones=5
```

Verification checklist: identical to `normal`. The parser must reassemble
correctly regardless of TCP segmentation boundaries.

### Scenario 3: `corrupt`

Restart server, then:

```bash
./build/dev/drone_client --scenario corrupt --host 127.0.0.1 --port 9000
```

Expected shutdown summary (100 iterations: 30 garbage, 20 bad-CRC, 50 valid):
```
packets_processed=50  crc_failures=20  malformed>=0  active_drones=5
```

Note: Garbage bytes do not directly increment `malformed_count_` — they are
absorbed by the resync/hunt-header logic. The CRC failures come from the 20
corrupt packets. Some garbage sequences may accidentally produce a valid-looking
header prefix and then fail the subsequent CRC check, so `crc_failures` may
exceed 20. That is correct behaviour.

Verification checklist:
- `packets_processed` close to 50 (valid packets in the stream)
- `crc_failures >= 20`
- No crash

### Scenario 4: `stress`

Restart server, then:

```bash
./build/dev/drone_client --scenario stress --host 127.0.0.1 --port 9000
```

Client log should show `>= 1000 pkt/s`.

Expected shutdown summary (exact count varies):
```
packets_processed=<N>  crc_failures=0  malformed=0  active_drones=1
```

Verification checklist:
- Client logs `>= 1000 pkt/s`
- `crc_failures=0`
- `malformed=0`
- No packet loss visible (server `packets_processed` should match client send count)

### Scenario 5: `alert`

Restart server, then:

```bash
./build/dev/drone_client --scenario alert --host 127.0.0.1 --port 9000
```

Expected server log contains (6 total alert lines):
```
[warning] [ALERT] drone=alert-drone-0 type=ALTITUDE state=ENTERED
[warning] [ALERT] drone=alert-drone-0 type=SPEED state=ENTERED
[warning] [ALERT] drone=alert-drone-1 type=ALTITUDE state=ENTERED
[warning] [ALERT] drone=alert-drone-1 type=SPEED state=ENTERED
[warning] [ALERT] drone=alert-drone-2 type=ALTITUDE state=ENTERED
[warning] [ALERT] drone=alert-drone-2 type=SPEED state=ENTERED
```

Verification checklist:
- ALTITUDE and SPEED alerts fired for all 3 drones
- No duplicate alerts for the same drone (state transition is one-shot)
- `packets_processed=15` (3 drones * 5 packets)
- `active_drones=3`

### Scenario 6: `multi-drone`

Restart server, then:

```bash
./build/dev/drone_client --scenario multi-drone --host 127.0.0.1 --port 9000
```

Expected shutdown summary:
```
packets_processed=1000  crc_failures=0  malformed=0  active_drones=100
```

Verification checklist:
- `active_drones=100`
- `packets_processed=1000`

### Scenario 7: `interleaved`

Restart server, then:

```bash
./build/dev/drone_client --scenario interleaved --host 127.0.0.1 --port 9000
```

Expected shutdown summary:
```
packets_processed=250  crc_failures=0  malformed=0  active_drones=5
```

Verification checklist:
- `active_drones=5` (no spurious extra drones from state leak)
- `crc_failures=0`
- No alerts fired (all telemetry is below thresholds)

---

## Clang-Tidy Considerations

| Pattern | Location | Resolution |
|---------|----------|------------|
| `argv` pointer arithmetic | `parseArgs` | `// NOLINT(cppcoreguidelines-pro-bounds-pointer-arithmetic)` on each `argv[++i]` |
| `reinterpret_cast<sockaddr*>` | `tcpConnect` | `// NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)` |
| `strerror(errno)` after syscall | `tcpConnect`, `sendAll` | `// NOLINT(concurrency-mt-unsafe)` — single-threaded client |
| Magic numbers in `runCorrupt` | `runCorrupt` | Replace with named constants `GarbageThreshold`, `CorruptThreshold` |
| `std::function` in map | `main()` | Performance is not a concern here; leave or use `// NOLINT` if warned |
| `int main()` vs `auto main()` | `main()` | Use `auto main(int argc, char** argv) -> int` for consistency with server |
| `send()` blocking annotation | `sendAll` | `// NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)` if triggered |

---

## File Summary

| File | Action |
|------|--------|
| `client/include/packet_builder.hpp` | Create |
| `client/src/packet_builder.cpp` | Create |
| `client/src/main.cpp` | Replace stub |
| `client/CMakeLists.txt` | Update (add `packet_builder.cpp`) |
| `tests/client/packet_builder_test.cpp` | Create (new directory `tests/client/`) |
| `tests/CMakeLists.txt` | Update (add test file, source file, include path) |

No domain, protocol, common, or server files are modified.

---

## Final `client/CMakeLists.txt`

```cmake
add_executable(drone_client
  src/packet_builder.cpp
  src/main.cpp
)
target_include_directories(drone_client PRIVATE include)
target_compile_features(drone_client PRIVATE cxx_std_20)
target_link_libraries(drone_client PRIVATE protocol domain spdlog::spdlog)
```

---

## Total Story Points: 11

| Task | Points |
|------|--------|
| 6a: PacketBuilder (header + stub + tests RED + impl GREEN + CMake) | 3 |
| 6b: Client main.cpp (7 scenarios + CLI + TCP helpers) | 8 |

---

## Implementation Order (Hard Constraint)

```
6a.1  Write packet_builder.hpp
6a.2  Write stub packet_builder.cpp
6a.3  Update client/CMakeLists.txt
6a.4  Write tests/client/packet_builder_test.cpp
6a.5  Update tests/CMakeLists.txt
6a.6  Build + test → RED (stubs return empty)
6a.7  Implement packet_builder.cpp fully
6a.8  Build + test → GREEN (all PacketBuilder tests pass)

6b.1  Write client/src/main.cpp (parseArgs + tcpConnect + sendAll + all scenarios + main)
6b.2  Build → GREEN (client compiles, existing tests still green)
6b.3  Integration test each scenario against live server
```

Do not begin 6b until 6a.8 is green.

---

## Edge Cases and Error Handling

### PacketBuilder

- `garbageBytes(0)` returns empty vector (handled — guard at start of function).
- `fragment({}, N)` returns empty vector (handled — guard at start of function).
- `fragment(packet, 0)` returns empty vector — chunk_size=0 would infinite-loop
  without the guard. The function returns empty vector; the caller sends nothing.
- `corruptCrc` XOR `0xFF`: the only value that XOR with `0xFF` produces the same
  bit pattern is impossible (0x00 XOR 0xFF = 0xFF, 0xFF XOR 0xFF = 0x00; no
  fixpoint exists). The CRC is guaranteed to change.

### Client main.cpp

- `tcpConnect` failure: throws `std::runtime_error`, caught in `main()` if the
  connection attempt itself is inside the try block, or propagates to `std::terminate`
  if outside. Wrap the `tcpConnect` call in main's try block or let it propagate —
  both are acceptable since connection failure is fatal for the client.
- `sendAll` failure: throws `std::runtime_error`. Caught in main's scenario try block.
  The file descriptor is closed in the catch path before returning 1.
- Scenario name not found: handled before `tcpConnect` — exits with error message
  without opening a socket.
- Server not running: `connect()` fails with ECONNREFUSED; `tcpConnect` throws.
