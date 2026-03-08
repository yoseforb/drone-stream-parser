#include <arpa/inet.h>
#include <array>
#include <cerrno>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <functional>
#include <netinet/in.h>
#include <span>
#include <stdexcept>
#include <string>
#include <sys/socket.h>
#include <thread>
#include <unordered_map>
#include <utility>

#include <spdlog/spdlog.h>

#include "alert_policy.hpp"
#include "packet_builder.hpp"
#include "telemetry.hpp"
#include "unique_socket.hpp"

namespace {

constexpr uint16_t DefaultPort = 9000;
constexpr int MinPort = 1;
constexpr int MaxPort = 65535;
constexpr int DroneCount = 5;
constexpr int NormalPacketCount = 1000;
constexpr int CorruptIterations = 100;
constexpr int GarbageThreshold = 3;
constexpr int CorruptThreshold = 5;
constexpr int GarbageSize = 32;
constexpr int StressDurationSec = 10;
constexpr int AlertDroneCount = 3;
constexpr int AlertPacketsPerDrone = 5;
constexpr double AlertAltitude = 150.0;
constexpr double AlertSpeed = 60.0;
constexpr int MultiDroneCount = 100;
constexpr int MultiPacketsPerDrone = 10;
constexpr int InterleavedDroneCount = 5;
constexpr int InterleavedRounds = 50;

struct Args {
  std::string scenario;
  std::string host{"127.0.0.1"};
  uint16_t port{DefaultPort};
};

auto parseArgs(int argc, char** argv) -> Args {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto ArgsSpan = std::span<char*>(argv, static_cast<std::size_t>(argc));
  Args args;
  for (std::size_t i = 1; i + 1 < ArgsSpan.size(); ++i) {
    auto arg = std::string(ArgsSpan[i]);
    if (arg == "--scenario") {
      args.scenario = ArgsSpan[i + 1];
    } else if (arg == "--host") {
      args.host = ArgsSpan[i + 1];
    } else if (arg == "--port") {
      int port_value = 0;
      try {
        port_value = std::stoi(ArgsSpan[i + 1]);
      } catch (const std::exception& ex) {
        spdlog::error("invalid --port value '{}': {}", ArgsSpan[i + 1],
                      ex.what());
        std::exit(EXIT_FAILURE); // NOLINT(concurrency-mt-unsafe)
      }
      if (port_value < MinPort || port_value > MaxPort) {
        spdlog::error("--port value {} out of range ({}-{})", port_value,
                      MinPort, MaxPort);
        std::exit(EXIT_FAILURE); // NOLINT(concurrency-mt-unsafe)
      }
      args.port = static_cast<uint16_t>(port_value);
    }
  }
  return args;
}

auto tcpConnect(const std::string& host, uint16_t port) -> UniqueSocket {
  UniqueSocket sock(::socket(AF_INET, SOCK_STREAM, 0));
  if (!sock) {
    throw std::runtime_error(
        std::string("socket() failed: ") +
        std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_port = htons(port);
  if (::inet_pton(AF_INET, host.c_str(), &addr.sin_addr) <= 0) {
    throw std::runtime_error("inet_pton() failed for host: " + host);
  }

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (::connect(sock.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) <
      0) {
    throw std::runtime_error(
        std::string("connect() failed: ") +
        std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  return sock;
}

void sendAll(int sock, std::span<const uint8_t> data) {
  while (!data.empty()) {
    auto sent = ::send(sock, data.data(), data.size(), MSG_NOSIGNAL);
    if (sent < 0) {
      throw std::runtime_error(
          std::string("send() failed: ") +
          std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
    }
    data = data.subspan(static_cast<std::size_t>(sent));
  }
}

auto makeTel(const std::string& id_str, double alt, double speed,
             uint64_t timestamp) -> Telemetry {
  return Telemetry{
      .drone_id = id_str,
      .latitude = 0.0,
      .longitude = 0.0,
      .altitude = alt,
      .speed = speed,
      .timestamp = timestamp,
  };
}

auto makeTelNormal(const std::string& id_str, uint64_t timestamp) -> Telemetry {
  constexpr double NormalAltitude = 50.0;
  constexpr double NormalSpeed = 20.0;
  return makeTel(id_str, NormalAltitude, NormalSpeed, timestamp);
}

// --- Scenarios ---

void runNormal(int sock) {
  spdlog::info("scenario=normal: sending {} valid packets across {} drones",
               NormalPacketCount, DroneCount);

  for (int i = 0; i < NormalPacketCount; ++i) {
    auto drone_id = "drone-" + std::to_string(i % DroneCount);
    auto tel = makeTelNormal(drone_id, static_cast<uint64_t>(i));
    auto pkt = PacketBuilder::validPacket(tel);
    sendAll(sock, pkt);
    std::this_thread::sleep_for(std::chrono::milliseconds(1));
  }

  spdlog::info("scenario=normal: done");
}

void runFragmented(int sock) {
  spdlog::info(
      "scenario=fragmented: sending {} packets fragmented into small chunks",
      NormalPacketCount);

  for (int i = 0; i < NormalPacketCount; ++i) {
    auto drone_id = "drone-" + std::to_string(i % DroneCount);
    auto tel = makeTelNormal(drone_id, static_cast<uint64_t>(i));
    auto pkt = PacketBuilder::validPacket(tel);
    constexpr int ChunkMod = 3;
    auto chunk_size = static_cast<std::size_t>((i % ChunkMod) + 1);
    auto chunks = PacketBuilder::fragment(pkt, chunk_size);
    for (const auto& chunk : chunks) {
      sendAll(sock, chunk);
    }
  }

  spdlog::info("scenario=fragmented: done");
}

void runCorrupt(int sock) {
  spdlog::info("scenario=corrupt: sending {} iterations (30%% garbage, 20%% "
               "corrupt CRC, 50%% valid)",
               CorruptIterations);

  constexpr int Mod = 10;

  for (int i = 0; i < CorruptIterations; ++i) {
    if (i % Mod < GarbageThreshold) {
      auto garbage = PacketBuilder::garbageBytes(GarbageSize);
      sendAll(sock, garbage);
    } else if (i % Mod < CorruptThreshold) {
      auto drone_id = "corrupt-drone-" + std::to_string(i % DroneCount);
      auto tel = makeTelNormal(drone_id, static_cast<uint64_t>(i));
      auto pkt = PacketBuilder::corruptCrc(tel);
      sendAll(sock, pkt);
    } else {
      auto drone_id = "corrupt-drone-" + std::to_string(i % DroneCount);
      auto tel = makeTelNormal(drone_id, static_cast<uint64_t>(i));
      auto pkt = PacketBuilder::validPacket(tel);
      sendAll(sock, pkt);
    }
  }

  spdlog::info("scenario=corrupt: done");
}

void runStress(int sock) {
  spdlog::info("scenario=stress: max-rate send for {}s", StressDurationSec);

  const auto Start = std::chrono::steady_clock::now();
  const auto Deadline = Start + std::chrono::seconds(StressDurationSec);
  uint64_t count = 0;

  while (std::chrono::steady_clock::now() < Deadline) {
    auto tel = makeTelNormal("stress-drone", count);
    auto pkt = PacketBuilder::validPacket(tel);
    sendAll(sock, pkt);
    ++count;
  }

  auto elapsed = std::chrono::steady_clock::now() - Start;
  auto elapsed_ms =
      std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count();
  constexpr double MsPerSec = 1000.0;
  double rate = (elapsed_ms > 0)
                    ? static_cast<double>(count) /
                          (static_cast<double>(elapsed_ms) / MsPerSec)
                    : 0.0;

  spdlog::info("scenario=stress: done. sent={} rate={:.0f} pkt/s", count, rate);
}

void runAlert(int sock) {
  spdlog::info("scenario=alert: sending packets above thresholds "
               "(altitude={:.1f}>{:.1f}, speed={:.1f}>{:.1f})",
               AlertAltitude, DefaultAltitudeLimit, AlertSpeed,
               DefaultSpeedLimit);

  for (int drone = 0; drone < AlertDroneCount; ++drone) {
    auto drone_id = "alert-drone-" + std::to_string(drone);
    for (int pkt_idx = 0; pkt_idx < AlertPacketsPerDrone; ++pkt_idx) {
      auto timestamp = (static_cast<uint64_t>(drone) * AlertPacketsPerDrone) +
                       static_cast<uint64_t>(pkt_idx);
      auto tel = makeTel(drone_id, AlertAltitude, AlertSpeed, timestamp);
      auto pkt = PacketBuilder::validPacket(tel);
      sendAll(sock, pkt);
    }
  }

  spdlog::info("scenario=alert: done");
}

void runMultiDrone(int sock) {
  spdlog::info("scenario=multi-drone: {} unique drones, {} packets each = {} "
               "total",
               MultiDroneCount, MultiPacketsPerDrone,
               MultiDroneCount * MultiPacketsPerDrone);

  for (int drone = 0; drone < MultiDroneCount; ++drone) {
    auto drone_id = "multi-" + std::to_string(drone);
    for (int pkt_idx = 0; pkt_idx < MultiPacketsPerDrone; ++pkt_idx) {
      auto timestamp = (static_cast<uint64_t>(drone) * MultiPacketsPerDrone) +
                       static_cast<uint64_t>(pkt_idx);
      auto tel = makeTelNormal(drone_id, timestamp);
      auto pkt = PacketBuilder::validPacket(tel);
      sendAll(sock, pkt);
    }
  }

  spdlog::info("scenario=multi-drone: done. expected_drones={}",
               MultiDroneCount);
}

void runInterleaved(int sock) {
  constexpr int TotalPackets = InterleavedDroneCount * InterleavedRounds;
  spdlog::info("scenario=interleaved: {} drones, {} rounds round-robin = {} "
               "total",
               InterleavedDroneCount, InterleavedRounds, TotalPackets);

  for (int round = 0; round < InterleavedRounds; ++round) {
    for (int drone = 0; drone < InterleavedDroneCount; ++drone) {
      auto drone_id = "inter-" + std::to_string(drone);
      auto timestamp = (static_cast<uint64_t>(round) * InterleavedDroneCount) +
                       static_cast<uint64_t>(drone);
      auto tel = makeTelNormal(drone_id, timestamp);
      auto pkt = PacketBuilder::validPacket(tel);
      sendAll(sock, pkt);
    }
  }

  spdlog::info("scenario=interleaved: done");
}

void runAll(int sock) {
  const std::array<std::pair<const char*, std::function<void(int)>>, 7>
      Scenarios{{
          {"normal", runNormal},
          {"fragmented", runFragmented},
          {"corrupt", runCorrupt},
          {"stress", runStress},
          {"alert", runAlert},
          {"multi-drone", runMultiDrone},
          {"interleaved", runInterleaved},
      }};

  spdlog::info("scenario=all: running {} scenarios in sequence",
               Scenarios.size());

  for (const auto& [name, func] : Scenarios) {
    spdlog::info("scenario=all: starting '{}'", name);
    func(sock);
    spdlog::info("scenario=all: finished '{}'", name);
  }

  spdlog::info("scenario=all: all scenarios complete");
}

} // namespace

auto main(int argc, char** argv) -> int {
  auto args = parseArgs(argc, argv);

  if (args.scenario.empty()) {
    spdlog::error(
        "usage: drone_client --scenario <name> [--host <ip>] [--port <n>]");
    spdlog::error("scenarios: normal, fragmented, corrupt, stress, alert, "
                  "multi-drone, interleaved, all");
    return 1;
  }

  const std::unordered_map<std::string, std::function<void(int)>> Dispatch{
      {"normal", runNormal},
      {"fragmented", runFragmented},
      {"corrupt", runCorrupt},
      {"stress", runStress},
      {"alert", runAlert},
      {"multi-drone", runMultiDrone},
      {"interleaved", runInterleaved},
      {"all", runAll},
  };

  auto iter = Dispatch.find(args.scenario);
  if (iter == Dispatch.end()) {
    spdlog::error("unknown scenario '{}'", args.scenario);
    return 1;
  }

  UniqueSocket sock;
  try {
    sock = tcpConnect(args.host, args.port);
  } catch (const std::runtime_error& ex) {
    spdlog::error("connection failed: {}", ex.what());
    return 1;
  }

  spdlog::info("connected to {}:{}", args.host, args.port);

  try {
    iter->second(sock.get());
  } catch (const std::exception& ex) {
    spdlog::error("scenario '{}' failed: {}", args.scenario, ex.what());
    return 1;
  }

  spdlog::info("disconnected from {}:{}", args.host, args.port);

  return 0;
}
