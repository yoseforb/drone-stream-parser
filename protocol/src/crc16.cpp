#include "crc16.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>

namespace {

constexpr uint16_t Polynomial = 0x1021U;
constexpr uint16_t HighBitMask = 0x8000U;
constexpr std::size_t TableSize = 256;
constexpr int BitsPerByte = 8;

// NOLINTNEXTLINE(llvm-prefer-static-over-anonymous-namespace)
constexpr auto buildTable() noexcept -> std::array<uint16_t, TableSize> {
  std::array<uint16_t, TableSize> table{};
  for (std::size_t i = 0; i < TableSize; ++i) {
    auto crc = static_cast<uint16_t>(i << BitsPerByte);
    for (int bit = 0; bit < BitsPerByte; ++bit) {
      if ((crc & HighBitMask) != 0U) {
        crc = static_cast<uint16_t>(static_cast<unsigned>(crc << 1U) ^
                                    Polynomial);
      } else {
        crc = static_cast<uint16_t>(crc << 1U);
      }
    }
    table.at(i) = crc;
  }
  return table;
}

constexpr auto CrcTable = buildTable();

} // namespace

uint16_t crc16(std::span<const uint8_t> data) noexcept {
  uint16_t crc = 0x0000U;
  for (uint8_t const Byte : data) {
    auto const Index = static_cast<uint8_t>((crc >> BitsPerByte) ^ Byte);
    crc = static_cast<uint16_t>(static_cast<unsigned>(crc << BitsPerByte) ^
                                CrcTable.at(Index));
  }
  return crc;
}
