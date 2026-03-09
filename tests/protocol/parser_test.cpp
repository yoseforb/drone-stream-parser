#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"
#include "packet_deserializer.hpp"
#include "packet_serializer.hpp"
#include "stream_parser.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

auto makeTel(const std::string& id, double alt, double speed,
             uint64_t timestamp) -> Telemetry {
  return {.drone_id = id,
          .latitude = 0.0,
          .longitude = 0.0,
          .altitude = alt,
          .speed = speed,
          .timestamp = timestamp};
}

/// Helper: create a parser that deserializes payloads into a results vector.
auto makeParser(std::vector<Telemetry>& results) -> StreamParser {
  return StreamParser{[&](std::span<const uint8_t> payload) {
    auto tel = PacketDeserializer::deserialize(payload);
    if (tel.has_value()) {
      results.push_back(std::move(*tel));
    }
  }};
}

} // namespace

TEST(StreamParserTest, SingleValidPacketCallsCallbackOnce) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto packet = PacketSerializer::serialize(makeTel("D1", 100.0, 20.0, 1000));
  parser.feed(packet);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D1");
  EXPECT_DOUBLE_EQ(results[0].altitude, 100.0);
  EXPECT_DOUBLE_EQ(results[0].speed, 20.0);
  EXPECT_EQ(results[0].timestamp, 1000U);
}

TEST(StreamParserTest, PacketSplitAcrossTwoFeedCallsCallsCallbackOnce) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto packet = PacketSerializer::serialize(makeTel("D2", 50.0, 10.0, 2000));
  auto full = std::span<const uint8_t>(packet);
  auto half = packet.size() / 2;

  parser.feed(full.subspan(0, half));
  EXPECT_TRUE(results.empty());

  parser.feed(full.subspan(half));
  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D2");
}

TEST(StreamParserTest, PacketFedOneByteAtATimeCallsCallbackOnce) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto packet = PacketSerializer::serialize(makeTel("D3", 0.0, 0.0, 3000));
  for (unsigned char& byte : packet) {
    parser.feed(std::span<const uint8_t>(&byte, 1));
  }

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D3");
}

TEST(StreamParserTest, TwoPacketsInOneFeedCallsCallbackTwice) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto pkt1 = PacketSerializer::serialize(makeTel("D4", 10.0, 5.0, 4000));
  auto pkt2 = PacketSerializer::serialize(makeTel("D5", 20.0, 6.0, 5000));

  std::vector<uint8_t> combined;
  combined.insert(combined.end(), pkt1.begin(), pkt1.end());
  combined.insert(combined.end(), pkt2.begin(), pkt2.end());

  parser.feed(combined);

  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0].drone_id, "D4");
  EXPECT_EQ(results[1].drone_id, "D5");
}

TEST(StreamParserTest, GarbageBytesBeforeValidPacketParserResyncs) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto packet = PacketSerializer::serialize(makeTel("D6", 30.0, 7.0, 6000));

  std::vector<uint8_t> data = {0x00, 0x11, 0xAA, 0x00, 0xFF};
  data.insert(data.end(), packet.begin(), packet.end());

  parser.feed(data);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D6");
  EXPECT_EQ(parser.getCrcFailCount(), 0U);
}

TEST(StreamParserTest, PacketWithBadCrcCallbackNotCalled) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto packet = PacketSerializer::serialize(makeTel("D7", 40.0, 8.0, 7000));
  packet[packet.size() - 1] ^= 0xFF;
  packet[packet.size() - 2] ^= 0xFF;

  parser.feed(packet);

  EXPECT_TRUE(results.empty());
  EXPECT_EQ(parser.getCrcFailCount(), 1U);
}

TEST(StreamParserTest,
     OversizedLengthPacketCallbackNotCalledAndResyncsToValid) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  // Length=5000 (0x1388) exceeds MAX_PAYLOAD=4096
  std::vector<uint8_t> bad_packet = {0xAA, 0x55, 0x88, 0x13};

  auto valid = PacketSerializer::serialize(makeTel("D8", 50.0, 9.0, 8000));

  std::vector<uint8_t> data;
  data.insert(data.end(), bad_packet.begin(), bad_packet.end());
  data.insert(data.end(), valid.begin(), valid.end());

  parser.feed(data);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D8");
  EXPECT_EQ(parser.getCrcFailCount(), 0U);
}

TEST(StreamParserTest, BadCrcPacketFollowedByValidPacketOnlyValidEmitted) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto pkt1 = PacketSerializer::serialize(makeTel("D8a", 60.0, 10.0, 8100));
  pkt1[pkt1.size() - 1] ^= 0xFF;
  pkt1[pkt1.size() - 2] ^= 0xFF;

  auto pkt2 = PacketSerializer::serialize(makeTel("D8b", 70.0, 11.0, 8200));

  std::vector<uint8_t> data;
  data.insert(data.end(), pkt1.begin(), pkt1.end());
  data.insert(data.end(), pkt2.begin(), pkt2.end());

  parser.feed(data);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D8b");
  EXPECT_EQ(parser.getCrcFailCount(), 1U);
}

TEST(StreamParserTest, GarbageThenValidThenGarbageThenValidBothValidEmitted) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  std::vector<uint8_t> garbage1 = {0x00, 0xFF, 0xBB};
  auto pkt1 = PacketSerializer::serialize(makeTel("D9", 80.0, 12.0, 9000));
  std::vector<uint8_t> garbage2 = {0x11, 0x22, 0x33, 0xAA};
  auto pkt2 = PacketSerializer::serialize(makeTel("D10", 90.0, 13.0, 10000));

  std::vector<uint8_t> data;
  data.insert(data.end(), garbage1.begin(), garbage1.end());
  data.insert(data.end(), pkt1.begin(), pkt1.end());
  data.insert(data.end(), garbage2.begin(), garbage2.end());
  data.insert(data.end(), pkt2.begin(), pkt2.end());

  parser.feed(data);

  ASSERT_EQ(results.size(), 2U);
  EXPECT_EQ(results[0].drone_id, "D9");
  EXPECT_EQ(results[1].drone_id, "D10");
  EXPECT_EQ(parser.getCrcFailCount(), 0U);
}

TEST(StreamParserTest, EmptyFeedDoesNothing) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  parser.feed(std::span<const uint8_t>{});

  EXPECT_TRUE(results.empty());
  EXPECT_EQ(parser.getCrcFailCount(), 0U);
}

TEST(StreamParserTest, EmptyDroneIdRoundTrips) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  auto packet = PacketSerializer::serialize(makeTel("", 100.0, 20.0, 1000));
  parser.feed(packet);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_TRUE(results[0].drone_id.empty());
}

TEST(StreamParserTest, HeaderBytesInsidePayloadDoNotConfuseParser) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  const std::string TrickyId = "X\xAA\x55Y";
  auto packet =
      PacketSerializer::serialize(makeTel(TrickyId, 50.0, 10.0, 2000));
  parser.feed(packet);

  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, TrickyId);
  EXPECT_DOUBLE_EQ(results[0].altitude, 50.0);
}

TEST(StreamParserTest, MalformedIdLenExceedingPayloadIsHandledByDeserializer) {
  std::vector<Telemetry> results;
  auto parser = makeParser(results);

  // Build a valid packet, then tamper IdLen to 0xFFFF
  auto bad_packet =
      PacketSerializer::serialize(makeTel("D1", 100.0, 20.0, 1000));

  // IdLen is at offset 4 (right after header+length)
  const uint16_t BadIdLen = 0xFFFF;
  std::memcpy(&bad_packet[4], &BadIdLen, sizeof(BadIdLen));

  // Recompute CRC over header+length+payload (everything except last 2 bytes)
  auto frame_span =
      std::span<const uint8_t>(bad_packet.data(), bad_packet.size() - 2);
  const uint16_t NewCrc = crc16(frame_span);
  std::memcpy(&bad_packet[bad_packet.size() - 2], &NewCrc, sizeof(NewCrc));

  // Append a valid packet after the tampered one
  auto valid_packet =
      PacketSerializer::serialize(makeTel("D2", 200.0, 30.0, 2000));

  std::vector<uint8_t> data;
  data.insert(data.end(), bad_packet.begin(), bad_packet.end());
  data.insert(data.end(), valid_packet.begin(), valid_packet.end());

  parser.feed(data);

  // Bad packet's payload passes CRC but deserialization fails (IdLen > payload)
  // The makeParser helper silently drops it; valid packet still arrives
  ASSERT_EQ(results.size(), 1U);
  EXPECT_EQ(results[0].drone_id, "D2");
}

// NOLINTEND(readability-magic-numbers)
