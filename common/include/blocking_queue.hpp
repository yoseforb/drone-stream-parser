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

  void push(T&& item) {
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_full_.wait(lock,
                     [this] { return closed_ || buffer_.size() < capacity_; });
      if (closed_) {
        return;
      }
      buffer_.push_back(std::move(item));
    }
    not_empty_.notify_one();
  }

  auto pop() -> std::optional<T> {
    std::optional<T> result;
    {
      std::unique_lock<std::mutex> lock(mutex_);
      not_empty_.wait(lock, [this] { return closed_ || !buffer_.empty(); });
      if (buffer_.empty()) {
        return std::nullopt;
      }
      result = std::move(buffer_.front());
      buffer_.pop_front();
    }
    not_full_.notify_one();
    return result;
  }

  void close() noexcept {
    {
      const std::scoped_lock Lock(mutex_);
      closed_ = true;
    }
    not_empty_.notify_all();
    not_full_.notify_all();
  }

private:
  size_t capacity_;
  std::deque<T> buffer_;
  std::mutex mutex_;
  std::condition_variable not_empty_;
  std::condition_variable not_full_;
  bool closed_{false};
};

#endif // BLOCKING_QUEUE_HPP
