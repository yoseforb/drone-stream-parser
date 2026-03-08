#ifndef STREAM_PARSER_HPP
#define STREAM_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

#include "telemetry.hpp"

class StreamParser {
public:
  explicit StreamParser(std::function<void(Telemetry)> on_packet);

  void feed(std::span<const uint8_t> chunk) noexcept;

  [[nodiscard]] uint64_t getCrcFailCount() const noexcept;
  [[nodiscard]] uint64_t getMalformedCount() const noexcept;

private:
  enum class State : uint8_t {
    HUNT_HEADER,
    READ_LENGTH,
    READ_PAYLOAD,
    READ_CRC,
  };

  std::function<void(Telemetry)> on_packet_;
  std::vector<uint8_t> buffer_;
  size_t read_pos_{0};         // NOLINT(clang-diagnostic-unused-private-field)
  size_t header_start_{0};     // NOLINT(clang-diagnostic-unused-private-field)
  uint16_t pending_length_{0}; // NOLINT(clang-diagnostic-unused-private-field)
  // NOLINTNEXTLINE(clang-diagnostic-unused-private-field)
  State state_{State::HUNT_HEADER};
  uint64_t crc_fail_count_{0};
  uint64_t malformed_count_{0};
};

#endif // STREAM_PARSER_HPP
