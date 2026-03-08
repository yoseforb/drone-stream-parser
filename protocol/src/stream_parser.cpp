#include "stream_parser.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "crc16.hpp"
#include "telemetry.hpp"

namespace {

static_assert(sizeof(double) == 8, // NOLINT(readability-magic-numbers)
              "Protocol requires IEEE 754 64-bit doubles");

constexpr uint8_t HeaderByte0 = 0xAAU;
constexpr uint8_t HeaderByte1 = 0x55U;
constexpr std::size_t MaxPayload = 4096U;
constexpr std::size_t HeaderSize = 2U;
constexpr std::size_t LengthFieldSize = 2U;
constexpr std::size_t CrcFieldSize = 2U;
constexpr std::size_t DoubleFieldSize = 8U;
constexpr std::size_t TimestampFieldSize = 8U;
constexpr std::size_t IdLenFieldSize = 2U;

// Minimum payload: IdLen(2) + 4*double(32) + timestamp(8) = 42
constexpr std::size_t MinFixedPayloadSize =
    IdLenFieldSize + (4U * DoubleFieldSize) + TimestampFieldSize;

auto readU16Le(const std::vector<uint8_t>& buf, std::size_t offset) noexcept
    -> uint16_t {
  uint16_t value = 0;
  std::memcpy(&value, &buf[offset], IdLenFieldSize);
  return value;
}

auto readDouble(const std::vector<uint8_t>& buf, std::size_t offset) noexcept
    -> double {
  double value = 0.0;
  std::memcpy(&value, &buf[offset], DoubleFieldSize);
  return value;
}

auto readU64Le(const std::vector<uint8_t>& buf, std::size_t offset) noexcept
    -> uint64_t {
  uint64_t value = 0;
  std::memcpy(&value, &buf[offset], TimestampFieldSize);
  return value;
}

} // namespace

StreamParser::StreamParser(std::function<void(Telemetry)> on_packet)
    : on_packet_(std::move(on_packet)) {}

void StreamParser::feed(std::span<const uint8_t> chunk) noexcept {
  buffer_.insert(buffer_.end(), chunk.begin(), chunk.end());

  for (;;) {
    bool progress = false;
    switch (state_) {
    case State::HUNT_HEADER:
      progress = huntHeader();
      break;
    case State::READ_LENGTH:
      progress = readLength();
      break;
    case State::READ_PAYLOAD:
      progress = readPayload();
      break;
    case State::READ_CRC:
      progress = readCrc();
      break;
    }
    if (!progress) {
      break;
    }
  }
}

auto StreamParser::huntHeader() noexcept -> bool {
  for (std::size_t i = read_pos_; i < buffer_.size(); ++i) {
    if (buffer_[i] != HeaderByte0) {
      continue;
    }
    header_start_ = i;
    if (i + 1 >= buffer_.size()) {
      read_pos_ = i;
      return false;
    }
    if (buffer_[i + 1] == HeaderByte1) {
      read_pos_ = i + HeaderSize;
      state_ = State::READ_LENGTH;
      return true;
    }
    read_pos_ = i + 1;
    return true;
  }
  read_pos_ = buffer_.size();
  return false;
}

auto StreamParser::readLength() noexcept -> bool {
  if (read_pos_ + LengthFieldSize > buffer_.size()) {
    return false;
  }
  pending_length_ = readU16Le(buffer_, read_pos_);
  read_pos_ += LengthFieldSize;
  if (static_cast<std::size_t>(pending_length_) > MaxPayload) {
    ++malformed_count_;
    resync();
    return true;
  }
  state_ = State::READ_PAYLOAD;
  return true;
}

auto StreamParser::readPayload() noexcept -> bool {
  if (read_pos_ + static_cast<std::size_t>(pending_length_) > buffer_.size()) {
    return false;
  }
  read_pos_ += static_cast<std::size_t>(pending_length_);
  state_ = State::READ_CRC;
  return true;
}

auto StreamParser::readCrc() noexcept -> bool {
  if (read_pos_ + CrcFieldSize > buffer_.size()) {
    return false;
  }
  uint16_t const ReceivedCrc = readU16Le(buffer_, read_pos_);
  read_pos_ += CrcFieldSize;

  auto const CrcDataLen =
      HeaderSize + LengthFieldSize + static_cast<std::size_t>(pending_length_);
  auto const ComputedCrc = crc16(
      std::span<const uint8_t>(buffer_).subspan(header_start_, CrcDataLen));

  if (ReceivedCrc != ComputedCrc) {
    ++crc_fail_count_;
    resync();
    return true;
  }

  auto tel = deserializePayload();
  if (!tel.has_value()) {
    ++malformed_count_;
    resync();
    return true;
  }

  buffer_.erase(buffer_.begin(),
                buffer_.begin() + static_cast<ptrdiff_t>(read_pos_));
  read_pos_ = 0;
  header_start_ = 0;
  pending_length_ = 0;
  state_ = State::HUNT_HEADER;

  on_packet_(std::move(*tel));
  return true;
}

void StreamParser::resync() noexcept {
  read_pos_ = header_start_ + 1;
  state_ = State::HUNT_HEADER;
}

auto StreamParser::deserializePayload() const noexcept
    -> std::optional<Telemetry> {
  std::size_t pos = header_start_ + HeaderSize + LengthFieldSize;

  uint16_t const IdLen = readU16Le(buffer_, pos);
  pos += IdLenFieldSize;

  if (static_cast<std::size_t>(IdLen) + MinFixedPayloadSize >
      static_cast<std::size_t>(pending_length_)) {
    return std::nullopt;
  }

  std::string drone_id(buffer_.begin() + static_cast<ptrdiff_t>(pos),
                       buffer_.begin() + static_cast<ptrdiff_t>(pos) +
                           static_cast<ptrdiff_t>(IdLen));
  pos += static_cast<std::size_t>(IdLen);

  double const Latitude = readDouble(buffer_, pos);
  pos += DoubleFieldSize;
  double const Longitude = readDouble(buffer_, pos);
  pos += DoubleFieldSize;
  double const Altitude = readDouble(buffer_, pos);
  pos += DoubleFieldSize;
  double const Speed = readDouble(buffer_, pos);
  pos += DoubleFieldSize;

  uint64_t const Timestamp = readU64Le(buffer_, pos);

  return Telemetry{.drone_id = std::move(drone_id),
                   .latitude = Latitude,
                   .longitude = Longitude,
                   .altitude = Altitude,
                   .speed = Speed,
                   .timestamp = Timestamp};
}

uint64_t StreamParser::getCrcFailCount() const noexcept {
  return crc_fail_count_;
}

uint64_t StreamParser::getMalformedCount() const noexcept {
  return malformed_count_;
}
