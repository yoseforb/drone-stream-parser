#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "packet_deserializer.hpp"
#include "packet_serializer.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

/// Extract the payload portion from a full serialized packet.
/// Skips header (2B) + length (2B), strips trailing CRC (2B).
auto extractPayload(const std::vector<uint8_t>& packet)
    -> std::vector<uint8_t> {
  constexpr std::size_t FrameOverhead = 4; // header + length
  constexpr std::size_t CrcSize = 2;
  return {packet.begin() + static_cast<std::ptrdiff_t>(FrameOverhead),
          packet.end() - static_cast<std::ptrdiff_t>(CrcSize)};
}

} // namespace

// --- Happy path (round-trip with serializer) ---

TEST(PacketDeserializerTest, ValidPacketRoundTripsAllFields) {
  const Telemetry Expected{.drone_id = "DRONE-42",
                           .latitude = 51.5074,
                           .longitude = -0.1278,
                           .altitude = 150.5,
                           .speed = 23.7,
                           .timestamp = 1700000000U};

  auto packet = PacketSerializer::serialize(Expected);
  auto payload = extractPayload(packet);

  auto result = PacketDeserializer::deserialize(payload);
  ASSERT_TRUE(result.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(result->drone_id, Expected.drone_id);
  EXPECT_DOUBLE_EQ(result->latitude, Expected.latitude);
  EXPECT_DOUBLE_EQ(result->longitude, Expected.longitude);
  EXPECT_DOUBLE_EQ(result->altitude, Expected.altitude);
  EXPECT_DOUBLE_EQ(result->speed, Expected.speed);
  EXPECT_EQ(result->timestamp, Expected.timestamp);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(PacketDeserializerTest, EmptyDroneIdRoundTrips) {
  const Telemetry Expected{.drone_id = "",
                           .latitude = 0.0,
                           .longitude = 0.0,
                           .altitude = 0.0,
                           .speed = 0.0,
                           .timestamp = 0U};

  auto packet = PacketSerializer::serialize(Expected);
  auto payload = extractPayload(packet);

  auto result = PacketDeserializer::deserialize(payload);
  ASSERT_TRUE(result.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(result->drone_id, "");
  EXPECT_DOUBLE_EQ(result->latitude, 0.0);
  EXPECT_DOUBLE_EQ(result->longitude, 0.0);
  EXPECT_DOUBLE_EQ(result->altitude, 0.0);
  EXPECT_DOUBLE_EQ(result->speed, 0.0);
  EXPECT_EQ(result->timestamp, 0U);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(PacketDeserializerTest, LongDroneIdRoundTrips) {
  const std::string LongId(200, 'X');
  const Telemetry Expected{.drone_id = LongId,
                           .latitude = -89.999,
                           .longitude = 179.999,
                           .altitude = 10000.0,
                           .speed = 999.99,
                           .timestamp = UINT64_MAX};

  auto packet = PacketSerializer::serialize(Expected);
  auto payload = extractPayload(packet);

  auto result = PacketDeserializer::deserialize(payload);
  ASSERT_TRUE(result.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(result->drone_id, LongId);
  EXPECT_DOUBLE_EQ(result->latitude, Expected.latitude);
  EXPECT_DOUBLE_EQ(result->longitude, Expected.longitude);
  EXPECT_DOUBLE_EQ(result->altitude, Expected.altitude);
  EXPECT_DOUBLE_EQ(result->speed, Expected.speed);
  EXPECT_EQ(result->timestamp, Expected.timestamp);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

// --- Malformed payload tests (return nullopt) ---

TEST(PacketDeserializerTest, EmptyPayloadReturnsNullopt) {
  std::vector<uint8_t> empty;
  auto result = PacketDeserializer::deserialize(empty);
  EXPECT_FALSE(result.has_value());
}

TEST(PacketDeserializerTest, PayloadShorterThanMinFixedSizeReturnsNullopt) {
  std::vector<uint8_t> too_short(41, 0x00);
  auto result = PacketDeserializer::deserialize(too_short);
  EXPECT_FALSE(result.has_value());
}

TEST(PacketDeserializerTest, IdLenExceedsRemainingPayloadReturnsNullopt) {
  // 42 bytes total, but id_len set to 0xFFFF
  std::vector<uint8_t> payload(42, 0x00);
  payload[0] = 0xFF;
  payload[1] = 0xFF;
  auto result = PacketDeserializer::deserialize(payload);
  EXPECT_FALSE(result.has_value());
}

TEST(PacketDeserializerTest, IdLenPlusFixedOverheadExceedsSizeReturnsNullopt) {
  // 42 bytes: MinFixedPayloadSize. Set id_len=1 so we'd need 43 bytes.
  std::vector<uint8_t> payload(42, 0x00);
  payload[0] = 0x01;
  payload[1] = 0x00;
  auto result = PacketDeserializer::deserialize(payload);
  EXPECT_FALSE(result.has_value());
}

// --- Edge cases ---

TEST(PacketDeserializerTest, ExactMinimumPayloadWithEmptyIdSucceeds) {
  // Build a 42-byte payload manually: id_len=0 + 4 doubles + 1 timestamp
  std::vector<uint8_t> payload(42, 0x00);
  // id_len = 0 (already zeroed), all fields zero is valid
  auto result = PacketDeserializer::deserialize(payload);
  ASSERT_TRUE(result.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(result->drone_id, "");
  EXPECT_DOUBLE_EQ(result->latitude, 0.0);
  EXPECT_DOUBLE_EQ(result->longitude, 0.0);
  EXPECT_DOUBLE_EQ(result->altitude, 0.0);
  EXPECT_DOUBLE_EQ(result->speed, 0.0);
  EXPECT_EQ(result->timestamp, 0U);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

TEST(PacketDeserializerTest, PayloadWithExtraTrailingBytesSucceeds) {
  const Telemetry Expected{.drone_id = "D1",
                           .latitude = 1.0,
                           .longitude = 2.0,
                           .altitude = 3.0,
                           .speed = 4.0,
                           .timestamp = 1000U};

  auto packet = PacketSerializer::serialize(Expected);
  auto payload = extractPayload(packet);

  // Append extra garbage bytes
  payload.push_back(0xDE);
  payload.push_back(0xAD);
  payload.push_back(0xBE);
  payload.push_back(0xEF);

  auto result = PacketDeserializer::deserialize(payload);
  ASSERT_TRUE(result.has_value());
  // NOLINTBEGIN(bugprone-unchecked-optional-access)
  EXPECT_EQ(result->drone_id, Expected.drone_id);
  EXPECT_DOUBLE_EQ(result->latitude, Expected.latitude);
  EXPECT_DOUBLE_EQ(result->longitude, Expected.longitude);
  EXPECT_DOUBLE_EQ(result->altitude, Expected.altitude);
  EXPECT_DOUBLE_EQ(result->speed, Expected.speed);
  EXPECT_EQ(result->timestamp, Expected.timestamp);
  // NOLINTEND(bugprone-unchecked-optional-access)
}

// NOLINTEND(readability-magic-numbers)
