#include "packet_builder.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <vector>

#include "packet_serializer.hpp"
#include "protocol_constants.hpp"
#include "telemetry.hpp"

namespace {
constexpr uint64_t LcgA = 6364136223846793005ULL;
constexpr uint64_t LcgC = 1442695040888963407ULL;
constexpr uint64_t LcgSeed = 0xDEADBEEFCAFEBABEULL;
constexpr unsigned LcgShift = 33U;
constexpr uint8_t CrcXorMask = 0xFFU;
constexpr uint16_t OversizeLength = 5000U;
constexpr uint8_t OversizeLengthLow =
    static_cast<uint8_t>(OversizeLength & 0xFFU);
constexpr uint8_t OversizeLengthHigh =
    static_cast<uint8_t>((OversizeLength >> 8U) & 0xFFU);
} // namespace

auto PacketBuilder::validPacket(const Telemetry& tel) -> std::vector<uint8_t> {
  return PacketSerializer::serialize(tel);
}

auto PacketBuilder::corruptCrc(const Telemetry& tel) -> std::vector<uint8_t> {
  auto data = PacketSerializer::serialize(tel);
  if (data.size() >= protocol::CrcFieldSize) {
    data[data.size() - 1] ^= CrcXorMask;
    data[data.size() - 2] ^= CrcXorMask;
  }
  return data;
}

auto PacketBuilder::garbageBytes(std::size_t count) -> std::vector<uint8_t> {
  if (count == 0) {
    return {};
  }

  uint64_t state = LcgSeed;

  std::vector<uint8_t> result;
  result.reserve(count);

  // Generate first byte, skip if it equals HeaderByte0
  while (result.empty()) {
    state = (state * LcgA) + LcgC;
    auto byte = static_cast<uint8_t>(state >> LcgShift);
    if (byte != protocol::HeaderByte0) {
      result.push_back(byte);
    }
  }

  // Fill remaining bytes freely
  while (result.size() < count) {
    state = (state * LcgA) + LcgC;
    result.push_back(static_cast<uint8_t>(state >> LcgShift));
  }

  return result;
}

auto PacketBuilder::oversizeLength() -> std::vector<uint8_t> {
  return {protocol::HeaderByte0, protocol::HeaderByte1, OversizeLengthLow,
          OversizeLengthHigh};
}

auto PacketBuilder::fragment(const std::vector<uint8_t>& packet,
                             std::size_t chunk_size)
    -> std::vector<std::vector<uint8_t>> {
  if (packet.empty() || chunk_size == 0) {
    return {};
  }

  std::vector<std::vector<uint8_t>> chunks;
  for (std::size_t offset = 0; offset < packet.size(); offset += chunk_size) {
    auto end = std::min(offset + chunk_size, packet.size());
    chunks.emplace_back(packet.begin() + static_cast<ptrdiff_t>(offset),
                        packet.begin() + static_cast<ptrdiff_t>(end));
  }
  return chunks;
}
