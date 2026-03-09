#include "telemetry_parser.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include "packet_deserializer.hpp"
#include "stream_parser.hpp"
#include "telemetry.hpp"

auto makeTelemetryParser(std::function<void(Telemetry)> on_telemetry)
    -> StreamParser {
  return StreamParser{
      [callback = std::move(on_telemetry)](std::span<const uint8_t> payload) {
        auto tel = PacketDeserializer::deserialize(payload);
        if (tel.has_value()) {
          callback(std::move(*tel));
        }
      }};
}
