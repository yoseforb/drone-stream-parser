#ifndef PROTOCOL_CONSTANTS_HPP
#define PROTOCOL_CONSTANTS_HPP

#include <cstddef>
#include <cstdint>

namespace protocol {
inline constexpr uint8_t HeaderByte0 = 0xAAU;
inline constexpr uint8_t HeaderByte1 = 0x55U;
inline constexpr std::size_t HeaderSize = 2U;
inline constexpr std::size_t LengthFieldSize = 2U;
inline constexpr std::size_t CrcFieldSize = 2U;
inline constexpr std::size_t IdLenFieldSize = 2U;
inline constexpr std::size_t DoubleFieldSize = 8U;
inline constexpr std::size_t TimestampFieldSize = 8U;
} // namespace protocol

#endif // PROTOCOL_CONSTANTS_HPP
