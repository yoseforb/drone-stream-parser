#include "packet_serializer.hpp"

#include <array>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>

#include "crc16.hpp"
#include "protocol_constants.hpp"
#include "telemetry.hpp"

namespace {

using protocol::CrcFieldSize;
using protocol::DoubleFieldSize;
using protocol::HeaderByte0;
using protocol::HeaderByte1;
using protocol::HeaderSize;
using protocol::IdLenFieldSize;
using protocol::LengthFieldSize;
using protocol::TimestampFieldSize;

static_assert(sizeof(double) == 8, // NOLINT(readability-magic-numbers)
              "Protocol requires IEEE 754 64-bit doubles");

constexpr std::size_t NumDoubleFields = 4;
constexpr std::size_t FixedPayloadOverhead =
    IdLenFieldSize + (NumDoubleFields * DoubleFieldSize) + TimestampFieldSize;

template <typename T> void appendLe(std::vector<uint8_t>& buf, T value) {
  std::array<uint8_t, sizeof(T)> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(T));
  buf.insert(buf.end(), bytes.begin(), bytes.end());
}

} // namespace

std::vector<uint8_t> PacketSerializer::serialize(const Telemetry& tel) {
  assert(tel.drone_id.size() <= UINT16_MAX - FixedPayloadOverhead &&
         "drone_id too large for uint16_t payload length");

  auto const PayloadSize = FixedPayloadOverhead + tel.drone_id.size();

  std::vector<uint8_t> packet;
  packet.reserve(HeaderSize + LengthFieldSize + PayloadSize + CrcFieldSize);

  // Header
  packet.push_back(HeaderByte0);
  packet.push_back(HeaderByte1);

  // Length (payload size as uint16_t LE)
  appendLe(packet, static_cast<uint16_t>(PayloadSize));

  // Payload: drone_id length prefix + drone_id bytes
  appendLe(packet, static_cast<uint16_t>(tel.drone_id.size()));
  for (char const Chr : tel.drone_id) {
    packet.push_back(static_cast<uint8_t>(Chr));
  }

  // Payload: doubles
  appendLe(packet, tel.latitude);
  appendLe(packet, tel.longitude);
  appendLe(packet, tel.altitude);
  appendLe(packet, tel.speed);

  // Payload: timestamp
  appendLe(packet, tel.timestamp);

  // CRC over header + length + payload
  auto const Checksum = crc16(packet);
  appendLe(packet, Checksum);

  return packet;
}
