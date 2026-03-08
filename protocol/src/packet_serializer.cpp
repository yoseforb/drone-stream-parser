#include "packet_serializer.hpp"

#include <array>
#include <cstdint>
#include <cstring>
#include <vector>

#include "crc16.hpp"
#include "telemetry.hpp"

namespace {

constexpr uint8_t HeaderByte0 = 0xAAU;
constexpr uint8_t HeaderByte1 = 0x55U;
constexpr std::size_t HeaderSize = 2;
constexpr std::size_t LengthFieldSize = 2;
constexpr std::size_t CrcFieldSize = 2;
constexpr std::size_t IdLenFieldSize = 2;
constexpr std::size_t DoubleFieldSize = 8;
constexpr std::size_t TimestampFieldSize = 8;
constexpr std::size_t NumDoubleFields = 4;

void appendU16Le(std::vector<uint8_t>& buf, uint16_t value) {
  std::array<uint8_t, LengthFieldSize> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(value));
  buf.insert(buf.end(), bytes.begin(), bytes.end());
}

void appendDouble(std::vector<uint8_t>& buf, double value) {
  std::array<uint8_t, DoubleFieldSize> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(value));
  buf.insert(buf.end(), bytes.begin(), bytes.end());
}

void appendU64Le(std::vector<uint8_t>& buf, uint64_t value) {
  std::array<uint8_t, TimestampFieldSize> bytes{};
  std::memcpy(bytes.data(), &value, sizeof(value));
  buf.insert(buf.end(), bytes.begin(), bytes.end());
}

} // namespace

std::vector<uint8_t> PacketSerializer::serialize(const Telemetry& tel) {
  auto const PayloadSize = IdLenFieldSize + tel.drone_id.size() +
                           (NumDoubleFields * DoubleFieldSize) +
                           TimestampFieldSize;

  std::vector<uint8_t> packet;
  packet.reserve(HeaderSize + LengthFieldSize + PayloadSize + CrcFieldSize);

  // Header
  packet.push_back(HeaderByte0);
  packet.push_back(HeaderByte1);

  // Length (payload size as uint16_t LE)
  appendU16Le(packet, static_cast<uint16_t>(PayloadSize));

  // Payload: drone_id length prefix + drone_id bytes
  appendU16Le(packet, static_cast<uint16_t>(tel.drone_id.size()));
  for (char const Chr : tel.drone_id) {
    packet.push_back(static_cast<uint8_t>(Chr));
  }

  // Payload: doubles
  appendDouble(packet, tel.latitude);
  appendDouble(packet, tel.longitude);
  appendDouble(packet, tel.altitude);
  appendDouble(packet, tel.speed);

  // Payload: timestamp
  appendU64Le(packet, tel.timestamp);

  // CRC over header + length + payload
  auto const Checksum = crc16(packet);
  appendU16Le(packet, Checksum);

  return packet;
}
