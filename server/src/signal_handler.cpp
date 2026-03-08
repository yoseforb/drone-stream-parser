#include "signal_handler.hpp"

#include <atomic>
#include <csignal>
#include <signal.h> // NOLINT(modernize-deprecated-headers) — sigaction is POSIX, not C++
#include <stdexcept>

#include <spdlog/spdlog.h>

namespace {

// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
std::atomic<std::atomic<bool>*> g_stop_flag{nullptr};

void handleSignal(int /*sig*/) {
  auto* flag = g_stop_flag.load(std::memory_order_relaxed);
  if (flag != nullptr) {
    flag->store(true, std::memory_order_relaxed);
  }
}

} // namespace

SignalHandler::SignalHandler(std::atomic<bool>& stop_flag) {
  auto* expected = static_cast<std::atomic<bool>*>(nullptr);
  if (!g_stop_flag.compare_exchange_strong(expected, &stop_flag,
                                           std::memory_order_relaxed)) {
    throw std::logic_error("SignalHandler: only one instance allowed");
  }

  struct sigaction sig_action{};
  sig_action.sa_handler = handleSignal;
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_flags = 0;
  if (sigaction(SIGINT, &sig_action, &old_sigint_) == -1) {
    spdlog::warn("SignalHandler: sigaction(SIGINT) failed");
  }
  if (sigaction(SIGTERM, &sig_action, &old_sigterm_) == -1) {
    spdlog::warn("SignalHandler: sigaction(SIGTERM) failed");
  }
}

SignalHandler::~SignalHandler() {
  g_stop_flag.store(nullptr, std::memory_order_relaxed);
  sigaction(SIGINT, &old_sigint_, nullptr);
  sigaction(SIGTERM, &old_sigterm_, nullptr);
}
