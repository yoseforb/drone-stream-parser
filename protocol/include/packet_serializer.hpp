#ifndef PACKET_SERIALIZER_HPP
#define PACKET_SERIALIZER_HPP

#include <cstdint>
#include <vector>

#include "telemetry.hpp"

class PacketSerializer {
public:
  [[nodiscard]] static std::vector<uint8_t> serialize(const Telemetry& tel);
};

#endif // PACKET_SERIALIZER_HPP
