#include "stream_parser.hpp"

#include <cstdint>
#include <functional>
#include <span>
#include <utility>

#include "telemetry.hpp"

StreamParser::StreamParser(std::function<void(Telemetry)> on_packet)
    : on_packet_(std::move(on_packet)) {}

void StreamParser::feed(std::span<const uint8_t> /*chunk*/) noexcept {}

uint64_t StreamParser::getCrcFailCount() const noexcept {
  return crc_fail_count_;
}

uint64_t StreamParser::getMalformedCount() const noexcept {
  return malformed_count_;
}
