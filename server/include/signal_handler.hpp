#ifndef SIGNAL_HANDLER_HPP
#define SIGNAL_HANDLER_HPP

#include <atomic>

class SignalHandler {
public:
  explicit SignalHandler(std::atomic<bool>& stop_flag);
};

#endif // SIGNAL_HANDLER_HPP
