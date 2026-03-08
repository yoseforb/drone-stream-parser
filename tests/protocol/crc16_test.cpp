#include <array>
#include <cstdint>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"

// NOLINTBEGIN(readability-magic-numbers)

TEST(Crc16Test, EmptySpanReturnsZero) {
  EXPECT_EQ(crc16(std::span<const uint8_t>{}), 0x0000U);
}

TEST(Crc16Test, SingleByteZeroReturnsZero) {
  std::array<uint8_t, 1> data{0x00};
  EXPECT_EQ(crc16(data), 0x0000U);
}

TEST(Crc16Test, SingleByte0x31Returns0x2672) {
  std::array<uint8_t, 1> data{0x31};
  EXPECT_EQ(crc16(data), 0x2672U);
}

TEST(Crc16Test, SingleByte0xFFReturns0x1EF0) {
  std::array<uint8_t, 1> data{0xFF};
  EXPECT_EQ(crc16(data), 0x1EF0U);
}

TEST(Crc16Test, HelloBytesReturn0xCBD6) {
  std::vector<uint8_t> data{0x48, 0x65, 0x6C, 0x6C, 0x6F};
  EXPECT_EQ(crc16(data), 0xCBD6U);
}

TEST(Crc16Test, Digits123456789ReturnStandardXmodemCheckValue) {
  std::vector<uint8_t> data{0x31, 0x32, 0x33, 0x34, 0x35,
                            0x36, 0x37, 0x38, 0x39};
  EXPECT_EQ(crc16(data), 0x31C3U);
}

// NOLINTEND(readability-magic-numbers)
