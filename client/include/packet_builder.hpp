#ifndef PACKET_BUILDER_HPP
#define PACKET_BUILDER_HPP

#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemetry.hpp"

/// Utility class for constructing telemetry packets in various states
/// (valid, corrupt, malformed) for use by the drone client scenarios.
///
/// All methods are static -- PacketBuilder has no state.
class PacketBuilder {
public:
  /// Returns a fully valid, CRC-correct framed packet.
  [[nodiscard]] static auto validPacket(const Telemetry& tel)
      -> std::vector<uint8_t>;

  /// Returns a valid packet with the last 2 bytes (CRC field) XORed with 0xFF,
  /// producing a guaranteed CRC mismatch.
  [[nodiscard]] static auto corruptCrc(const Telemetry& tel)
      -> std::vector<uint8_t>;

  /// Returns `count` pseudo-random bytes that do not begin with the header
  /// sequence [0xAA, 0x55]. The server parser must skip these via resync.
  [[nodiscard]] static auto garbageBytes(std::size_t count)
      -> std::vector<uint8_t>;

  /// Returns a 4-byte frame with header [0xAA, 0x55] and LENGTH=5000 (0x1388)
  /// little-endian, which exceeds the parser's MAX_PAYLOAD (4096).
  /// The server increments its malformed counter and resyncs.
  [[nodiscard]] static auto oversizeLength() -> std::vector<uint8_t>;

  /// Splits `packet` into chunks of at most `chunk_size` bytes.
  /// The caller sends each chunk as a separate TCP send() call.
  [[nodiscard]] static auto fragment(const std::vector<uint8_t>& packet,
                                     std::size_t chunk_size)
      -> std::vector<std::vector<uint8_t>>;
};

#endif // PACKET_BUILDER_HPP
