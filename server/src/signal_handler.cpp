#include "signal_handler.hpp"

#include <atomic>
#include <csignal>
#include <signal.h> // NOLINT(modernize-deprecated-headers) — sigaction is POSIX, not C++

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
  g_stop_flag.store(&stop_flag, std::memory_order_relaxed);

  struct sigaction sig_action{};
  sig_action.sa_handler = handleSignal;
  sigemptyset(&sig_action.sa_mask);
  sig_action.sa_flags = 0;
  sigaction(SIGINT, &sig_action, nullptr);
  sigaction(SIGTERM, &sig_action, nullptr);
}
