#ifndef PACKET_DESERIALIZER_HPP
#define PACKET_DESERIALIZER_HPP

#include <cstdint>
#include <optional>
#include <span>

#include "telemetry.hpp"

/// Deserializes raw payload bytes into a Telemetry struct.
///
/// The payload is the portion of a framed packet between the length field
/// and the CRC — no header, no length prefix, no trailing CRC.
class PacketDeserializer {
public:
  static auto deserialize(std::span<const uint8_t> payload) noexcept
      -> std::optional<Telemetry>;
};

#endif // PACKET_DESERIALIZER_HPP
