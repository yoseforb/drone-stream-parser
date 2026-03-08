#ifndef BLOCKING_QUEUE_HPP
#define BLOCKING_QUEUE_HPP

#include <condition_variable>
#include <cstddef>
#include <deque>
#include <mutex>
#include <optional>

template <typename T> class BlockingQueue {
public:
  explicit BlockingQueue(size_t capacity) : capacity_(capacity) {}

  // NOLINTNEXTLINE(cppcoreguidelines-rvalue-reference-param-not-moved)
  void push(T&& /*item*/) {}
  auto pop() -> std::optional<T> { return std::nullopt; }
  void close() noexcept {}

private:
  size_t capacity_;
  std::deque<T> buffer_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool closed_{false};
};

#endif // BLOCKING_QUEUE_HPP
