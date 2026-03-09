#include "packet_deserializer.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string>
#include <utility>

#include "protocol_constants.hpp"
#include "telemetry.hpp"

namespace {

using protocol::DoubleFieldSize;
using protocol::IdLenFieldSize;
using protocol::TimestampFieldSize;

// Minimum payload: IdLen(2) + 4*double(32) + timestamp(8) = 42
constexpr std::size_t MinFixedPayloadSize =
    IdLenFieldSize + (4U * DoubleFieldSize) + TimestampFieldSize;

template <typename T>
auto readLe(const uint8_t* data, std::size_t offset) noexcept -> T {
  T value{};
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  std::memcpy(&value, data + offset, sizeof(T));
  return value;
}

} // namespace

auto PacketDeserializer::deserialize(std::span<const uint8_t> payload) noexcept
    -> std::optional<Telemetry> {
  if (payload.size() < MinFixedPayloadSize) {
    return std::nullopt;
  }

  std::size_t pos = 0;

  auto const IdLen = readLe<uint16_t>(payload.data(), pos);
  pos += IdLenFieldSize;

  if (static_cast<std::size_t>(IdLen) + MinFixedPayloadSize > payload.size()) {
    return std::nullopt;
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  std::string drone_id(payload.data() + pos, payload.data() + pos + IdLen);
  pos += static_cast<std::size_t>(IdLen);

  auto const Latitude = readLe<double>(payload.data(), pos);
  pos += DoubleFieldSize;
  auto const Longitude = readLe<double>(payload.data(), pos);
  pos += DoubleFieldSize;
  auto const Altitude = readLe<double>(payload.data(), pos);
  pos += DoubleFieldSize;
  auto const Speed = readLe<double>(payload.data(), pos);
  pos += DoubleFieldSize;

  auto const Timestamp = readLe<uint64_t>(payload.data(), pos);

  return Telemetry{.drone_id = std::move(drone_id),
                   .latitude = Latitude,
                   .longitude = Longitude,
                   .altitude = Altitude,
                   .speed = Speed,
                   .timestamp = Timestamp};
}
