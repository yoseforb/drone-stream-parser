#include "packet_serializer.hpp"

#include <cstdint>
#include <vector>

#include "telemetry.hpp"

std::vector<uint8_t> PacketSerializer::serialize(const Telemetry& /*tel*/) {
  return {};
}
