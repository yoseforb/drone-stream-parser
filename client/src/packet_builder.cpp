#include "packet_builder.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

#include "telemetry.hpp"

auto PacketBuilder::validPacket(const Telemetry& /*tel*/)
    -> std::vector<uint8_t> {
  return {};
}

auto PacketBuilder::corruptCrc(const Telemetry& /*tel*/)
    -> std::vector<uint8_t> {
  return {};
}

auto PacketBuilder::garbageBytes(std::size_t /*count*/)
    -> std::vector<uint8_t> {
  return {};
}

auto PacketBuilder::oversizeLength() -> std::vector<uint8_t> { return {}; }

auto PacketBuilder::fragment(const std::vector<uint8_t>& /*packet*/,
                             std::size_t /*chunk_size*/)
    -> std::vector<std::vector<uint8_t>> {
  return {};
}
