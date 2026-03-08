#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <exception>
#include <span>
#include <stdexcept>
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
constexpr int MinPort = 1;
constexpr int MaxPort = 65535;
constexpr size_t QueueCapacity = 256;

auto parsePort(int argc, char** argv) -> uint16_t {
  // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
  const auto Args = std::span<char*>(argv, static_cast<std::size_t>(argc));
  for (std::size_t i = 1; i + 1 < Args.size(); ++i) {
    auto arg = std::string(Args[i]);
    if (arg == "--port") {
      int port_value = 0;
      try {
        port_value = std::stoi(Args[i + 1]);
      } catch (const std::exception& ex) {
        spdlog::error("invalid --port value '{}': {}", Args[i + 1], ex.what());
        std::exit(EXIT_FAILURE); // NOLINT(concurrency-mt-unsafe)
      }
      if (port_value < MinPort || port_value > MaxPort) {
        spdlog::error("--port value {} out of range ({}-{})", port_value,
                      MinPort, MaxPort);
        std::exit(EXIT_FAILURE); // NOLINT(concurrency-mt-unsafe)
      }
      return static_cast<uint16_t>(port_value);
    }
  }
  return DefaultPort;
}

struct Pipeline {
  BlockingQueue<std::vector<uint8_t>> raw_queue{QueueCapacity};
  BlockingQueue<Telemetry> parsed_queue{QueueCapacity};
  // Atomic for clarity of intent. The join() provides the actual
  // happens-before guarantee for the read in main().
  std::atomic<uint64_t> packets_processed{0};
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
    pipeline.packets_processed.fetch_add(1, std::memory_order_relaxed);
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

// NOLINTNEXTLINE(readability-function-size)
auto main(int argc, char** argv) -> int {
  const uint16_t Port = parsePort(argc, argv);

  const AlertPolicy Policy{};
  InMemoryDroneRepository repo{};
  ConsoleAlertNotifier notifier{};
  ProcessTelemetry use_case{repo, notifier, Policy};
  Pipeline pipeline{};

  std::atomic<bool> stop_flag{false};
  [[maybe_unused]] const SignalHandler SignalGuard{stop_flag};

  try {
    TcpServer server{Port, pipeline.raw_queue, stop_flag};

    spdlog::info("drone_server starting on port {}. altitude_limit={:.1f}m "
                 "speed_limit={:.1f}m/s",
                 Port, Policy.altitude_limit, Policy.speed_limit);

    StreamParser parser{[&pipeline](Telemetry tel) {
      pipeline.parsed_queue.push(std::move(tel));
    }};

    runPipeline(server, pipeline, parser, use_case);

    spdlog::info(
        "Shutdown complete. packets_processed={} crc_failures={} malformed={} "
        "active_drones={}",
        pipeline.packets_processed.load(std::memory_order_relaxed),
        parser.getCrcFailCount(), parser.getMalformedCount(), repo.size());
  } catch (const std::runtime_error& ex) {
    spdlog::error("{}", ex.what());
    return EXIT_FAILURE;
  }

  return 0;
}
