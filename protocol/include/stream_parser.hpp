#ifndef STREAM_PARSER_HPP
#define STREAM_PARSER_HPP

#include <cstddef>
#include <cstdint>
#include <functional>
#include <span>
#include <vector>

/// Streaming parser for drone telemetry packets.
///
/// Handles framing only: finding packet boundaries (header + length + CRC
/// validation). Delivers raw payload bytes to the callback for deserialization
/// by the caller.
///
/// Internally buffers bytes via std::vector which may allocate. If allocation
/// fails inside a noexcept method, std::terminate is called. This is by design:
/// OOM during stream parsing is unrecoverable in an embedded context.
class StreamParser {
public:
  /// @param on_packet Callback invoked with raw payload bytes for each valid
  ///        frame. Must not throw — throwing from the callback violates
  ///        noexcept and calls std::terminate.
  explicit StreamParser(
      std::function<void(std::span<const uint8_t>)> on_packet);

  /// Append raw bytes to the internal buffer and parse any complete packets.
  void feed(std::span<const uint8_t> chunk) noexcept;

  [[nodiscard]] uint64_t getCrcFailCount() const noexcept;

private:
  enum class State : uint8_t {
    HUNT_HEADER,
    READ_LENGTH,
    READ_PAYLOAD,
    READ_CRC,
    COMPLETE_FRAME,
  };

  auto huntHeader() noexcept -> bool;
  auto readLength() noexcept -> bool;
  auto readPayload() noexcept -> bool;
  auto readCrc() noexcept -> bool;
  void completeFrame() noexcept;
  void resync() noexcept;

  std::function<void(std::span<const uint8_t>)> on_packet_;
  std::vector<uint8_t> buffer_;
  size_t read_pos_{0};
  size_t header_start_{0};
  uint16_t pending_length_{0};
  State state_{State::HUNT_HEADER};
  uint64_t crc_fail_count_{0};
};

#endif // STREAM_PARSER_HPP
