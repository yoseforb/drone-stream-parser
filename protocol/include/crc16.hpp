#ifndef CRC16_HPP
#define CRC16_HPP

#include <cstdint>
#include <span>

[[nodiscard]] uint16_t crc16(std::span<const uint8_t> data) noexcept;

#endif // CRC16_HPP
