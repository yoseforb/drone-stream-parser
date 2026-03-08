#include <cstdint>
#include <cstring>
#include <span>
#include <string>
#include <vector>

#include <gtest/gtest.h>

#include "crc16.hpp"
#include "packet_builder.hpp"
#include "telemetry.hpp"

// NOLINTBEGIN(readability-magic-numbers)

namespace {

auto makeTel(const std::string& id) -> Telemetry {
  return {.drone_id = id,
          .latitude = 1.0,
          .longitude = 2.0,
          .altitude = 50.0,
          .speed = 10.0,
          .timestamp = 1000U};
}

} // namespace

// --- validPacket ---

TEST(PacketBuilderTest, ValidPacketStartsWithHeaderBytes) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketBuilderTest, ValidPacketCrcMatchesCrc16OverFrame) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  ASSERT_GE(packet.size(), 4U);
  auto frame_span = std::span<const uint8_t>(packet.data(), packet.size() - 2);
  auto expected_crc = crc16(frame_span);
  uint16_t actual_crc = 0;
  std::memcpy(&actual_crc, &packet[packet.size() - 2], sizeof(actual_crc));
  EXPECT_EQ(actual_crc, expected_crc);
}

TEST(PacketBuilderTest, ValidPacketIsNonEmpty) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  EXPECT_FALSE(packet.empty());
}

// --- corruptCrc ---

TEST(PacketBuilderTest, CorruptCrcStartsWithHeaderBytes) {
  auto packet = PacketBuilder::corruptCrc(makeTel("D1"));
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketBuilderTest, CorruptCrcDoesNotMatchCrc16OverFrame) {
  auto packet = PacketBuilder::corruptCrc(makeTel("D1"));
  ASSERT_GE(packet.size(), 4U);
  auto frame_span = std::span<const uint8_t>(packet.data(), packet.size() - 2);
  auto expected_crc = crc16(frame_span);
  uint16_t actual_crc = 0;
  std::memcpy(&actual_crc, &packet[packet.size() - 2], sizeof(actual_crc));
  EXPECT_NE(actual_crc, expected_crc);
}

TEST(PacketBuilderTest, CorruptCrcHasSameSizeAsValidPacket) {
  auto tel = makeTel("D1");
  auto valid = PacketBuilder::validPacket(tel);
  auto corrupt = PacketBuilder::corruptCrc(tel);
  EXPECT_EQ(valid.size(), corrupt.size());
}

// --- garbageBytes ---

TEST(PacketBuilderTest, GarbageBytesReturnsRequestedSize) {
  auto garbage = PacketBuilder::garbageBytes(100);
  EXPECT_EQ(garbage.size(), 100U);
}

TEST(PacketBuilderTest, GarbageBytesDoesNotStartWithHeaderSequence) {
  for (int i = 0; i < 100; ++i) {
    auto garbage = PacketBuilder::garbageBytes(50);
    ASSERT_GE(garbage.size(), 2U);
    const bool StartsWithHeader = (garbage[0] == 0xAAU && garbage[1] == 0x55U);
    EXPECT_FALSE(StartsWithHeader)
        << "Garbage bytes started with header on iteration " << i;
  }
}

TEST(PacketBuilderTest, GarbageBytesReturnsEmptyForCountZero) {
  auto garbage = PacketBuilder::garbageBytes(0);
  EXPECT_TRUE(garbage.empty());
}

// --- oversizeLength ---

TEST(PacketBuilderTest, OversizeLengthStartsWithHeader) {
  auto packet = PacketBuilder::oversizeLength();
  ASSERT_GE(packet.size(), 2U);
  EXPECT_EQ(packet[0], 0xAAU);
  EXPECT_EQ(packet[1], 0x55U);
}

TEST(PacketBuilderTest, OversizeLengthFieldEquals5000) {
  auto packet = PacketBuilder::oversizeLength();
  ASSERT_GE(packet.size(), 4U);
  uint16_t length = 0;
  std::memcpy(&length, &packet[2], sizeof(length));
  EXPECT_EQ(length, 5000U);
}

TEST(PacketBuilderTest, OversizeLengthTotalSizeIs4Bytes) {
  auto packet = PacketBuilder::oversizeLength();
  EXPECT_EQ(packet.size(), 4U);
}

// --- fragment ---

TEST(PacketBuilderTest, FragmentConcatenationReproducesOriginal) {
  auto packet = PacketBuilder::validPacket(makeTel("DRONE42"));
  auto chunks = PacketBuilder::fragment(packet, 10);

  std::vector<uint8_t> reassembled;
  for (const auto& chunk : chunks) {
    reassembled.insert(reassembled.end(), chunk.begin(), chunk.end());
  }
  EXPECT_EQ(reassembled, packet);
}

TEST(PacketBuilderTest, FragmentChunksExceptLastHaveExactSize) {
  auto packet = PacketBuilder::validPacket(makeTel("DRONE42"));
  const std::size_t ChunkSize = 10;
  auto chunks = PacketBuilder::fragment(packet, ChunkSize);

  ASSERT_FALSE(chunks.empty());
  for (std::size_t i = 0; i + 1 < chunks.size(); ++i) {
    EXPECT_EQ(chunks[i].size(), ChunkSize) << "Chunk " << i << " wrong size";
  }
}

TEST(PacketBuilderTest, FragmentLargerThanPacketReturnsSingleChunk) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  auto chunks = PacketBuilder::fragment(packet, packet.size() + 100);
  ASSERT_EQ(chunks.size(), 1U);
  EXPECT_EQ(chunks[0], packet);
}

TEST(PacketBuilderTest, FragmentChunkSizeOneProducesOneChunkPerByte) {
  auto packet = PacketBuilder::validPacket(makeTel("D1"));
  auto chunks = PacketBuilder::fragment(packet, 1);
  EXPECT_EQ(chunks.size(), packet.size());
  for (std::size_t i = 0; i < chunks.size(); ++i) {
    ASSERT_EQ(chunks[i].size(), 1U);
    EXPECT_EQ(chunks[i][0], packet[i]);
  }
}

// NOLINTEND(readability-magic-numbers)
