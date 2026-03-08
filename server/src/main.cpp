#include <atomic>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <thread>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "alert_policy.hpp"
#include "blocking_queue.hpp"
#include "console_alert_notifier.hpp"
#include "in_memory_drone_repo.hpp"
#include "process_telemetry.hpp"
#include "signal_handler.hpp"
#include "stream_parser.hpp"
#include "tcp_server.hpp"
#include "telemetry.hpp"

namespace {

constexpr uint16_t DefaultPort = 9000;
constexpr size_t QueueCapacity = 256;

// NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
auto parsePort(int argc, char** argv) -> uint16_t {
  for (int i = 1; i + 1 < argc; ++i) {
    // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
    auto arg = std::string(argv[i]);
    if (arg == "--port") {
      // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
      return static_cast<uint16_t>(std::stoi(argv[i + 1]));
    }
  }
  return DefaultPort;
}

struct Pipeline {
  BlockingQueue<std::vector<uint8_t>> raw_queue{QueueCapacity};
  BlockingQueue<Telemetry> parsed_queue{QueueCapacity};
  uint64_t packets_processed{0};
};

void runParseStage(Pipeline& pipeline, StreamParser& parser) {
  while (auto chunk = pipeline.raw_queue.pop()) {
    parser.feed(std::span<const uint8_t>{chunk->data(), chunk->size()});
  }
  pipeline.parsed_queue.close();
}

void runProcessStage(Pipeline& pipeline, ProcessTelemetry& use_case) {
  while (auto tel = pipeline.parsed_queue.pop()) {
    use_case.execute(*tel);
    ++pipeline.packets_processed;
  }
}

void runPipeline(TcpServer& server, Pipeline& pipeline, StreamParser& parser,
                 ProcessTelemetry& use_case) {
  std::thread recv_thread{[&server]() { server.run(); }};

  std::thread parse_thread{
      [&pipeline, &parser]() { runParseStage(pipeline, parser); }};

  std::thread process_thread{
      [&pipeline, &use_case]() { runProcessStage(pipeline, use_case); }};

  process_thread.join();
  parse_thread.join();
  recv_thread.join();
}

} // namespace

auto main(int argc, char** argv) -> int {
  uint16_t port = parsePort(argc, argv);

  // Domain + port adapters
  AlertPolicy policy{};
  InMemoryDroneRepository repo{};
  ConsoleAlertNotifier notifier{};
  ProcessTelemetry use_case{repo, notifier, policy};

  // Pipeline queues and state
  Pipeline pipeline{};

  // Infrastructure
  std::atomic<bool> stop_flag{false};
  // NOLINTNEXTLINE(misc-const-correctness)
  [[maybe_unused]] SignalHandler signal_handler{stop_flag};
  TcpServer server{port, pipeline.raw_queue, stop_flag};

  spdlog::info("drone_server starting on port {}. altitude_limit={:.1f}m "
               "speed_limit={:.1f}m/s",
               port, policy.altitude_limit, policy.speed_limit);

  StreamParser parser{[&pipeline](Telemetry tel) {
    pipeline.parsed_queue.push(std::move(tel));
  }};

  runPipeline(server, pipeline, parser, use_case);

  spdlog::info(
      "Shutdown complete. packets_processed={} crc_failures={} malformed={} "
      "active_drones={}",
      pipeline.packets_processed, parser.getCrcFailCount(),
      parser.getMalformedCount(), repo.size());

  return 0;
}
