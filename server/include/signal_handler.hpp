#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

#include <atomic>
#include <signal.h> // NOLINT(modernize-deprecated-headers) — sigaction is POSIX

class SignalHandler {
public:
  explicit SignalHandler(std::atomic<bool>& stop_flag);
  ~SignalHandler();

  SignalHandler(const SignalHandler&) = delete;
  SignalHandler& operator=(const SignalHandler&) = delete;
  SignalHandler(SignalHandler&&) = delete;
  SignalHandler& operator=(SignalHandler&&) = delete;

private:
  struct sigaction old_sigint_{};
  struct sigaction old_sigterm_{};
};

#endif // SIGNAL_HANDLER_HPP
