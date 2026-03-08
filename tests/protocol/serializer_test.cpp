#include <cstdint>
#include <cstring>
#include <span>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"
#include "packet_serializer.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

auto makeSimpleTelemetry() -> Telemetry {
  return Telemetry{.drone_id = "D1",
                   .latitude = 1.0,
                   .longitude = 2.0,
                   .altitude = 3.0,
                   .speed = 4.0,
                   .timestamp = 1000U};
}

} // namespace

TEST(PacketSerializerTest, OutputStartsWithHeaderBytes) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketSerializerTest, LengthFieldMatchesPayloadSize) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 4U);
  uint16_t length = 0;
  std::memcpy(&length, &packet[2], sizeof(length));
  EXPECT_EQ(length, 44U);
}

TEST(PacketSerializerTest, PayloadStartsWithDroneIdLengthPrefix) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 6U);
  EXPECT_EQ(packet[4], 0x02U);
  EXPECT_EQ(packet[5], 0x00U);
}

TEST(PacketSerializerTest, PayloadContainsDroneIdBytes) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 8U);
  EXPECT_EQ(packet[6], 0x44U);
  EXPECT_EQ(packet[7], 0x31U);
}

TEST(PacketSerializerTest, LatitudeEncodedAsLittleEndianDouble) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 16U);
  double latitude = 0.0;
  std::memcpy(&latitude, &packet[8], sizeof(latitude));
  EXPECT_DOUBLE_EQ(latitude, 1.0);
}

TEST(PacketSerializerTest, TimestampEncodedAsLittleEndianUint64) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 48U);
  uint64_t timestamp = 0;
  std::memcpy(&timestamp, &packet[40], sizeof(timestamp));
  EXPECT_EQ(timestamp, 1000U);
}

TEST(PacketSerializerTest, CrcAtEndMatchesCrc16OverFullFrame) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  ASSERT_GE(packet.size(), 4U);
  auto frame_span = std::span<const uint8_t>(packet.data(), packet.size() - 2);
  auto expected_crc = crc16(frame_span);
  uint16_t actual_crc = 0;
  std::memcpy(&actual_crc, &packet[packet.size() - 2], sizeof(actual_crc));
  EXPECT_EQ(actual_crc, expected_crc);
}

TEST(PacketSerializerTest, TotalSizeIs6PlusPayloadSize) {
  auto packet = PacketSerializer::serialize(makeSimpleTelemetry());
  EXPECT_EQ(packet.size(), 50U);
}

// NOLINTEND(readability-magic-numbers)
