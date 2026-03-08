#ifndef UNIQUE_SOCKET_HPP
#define UNIQUE_SOCKET_HPP

#include <unistd.h>

#include <utility>

class UniqueSocket {
public:
  UniqueSocket() noexcept = default;

  explicit UniqueSocket(int raw_fd) noexcept : fd_(raw_fd) {}

  ~UniqueSocket() noexcept {
    if (fd_ >= 0) {
      ::close(fd_);
    }
  }

  UniqueSocket(const UniqueSocket&) = delete;
  auto operator=(const UniqueSocket&) -> UniqueSocket& = delete;

  UniqueSocket(UniqueSocket&& other) noexcept : fd_(other.fd_) {
    other.fd_ = -1;
  }

  auto operator=(UniqueSocket&& other) noexcept -> UniqueSocket& {
    if (this != &other) {
      reset(other.release());
    }
    return *this;
  }

  [[nodiscard]] auto get() const noexcept -> int { return fd_; }

  auto release() noexcept -> int {
    const int OldFd = fd_;
    fd_ = -1;
    return OldFd;
  }

  void reset(int raw_fd = -1) noexcept {
    if (fd_ != raw_fd) {
      if (fd_ >= 0) {
        ::close(fd_);
      }
    }
    fd_ = raw_fd;
  }

  explicit operator bool() const noexcept { return fd_ >= 0; }

private:
  int fd_{-1};
};

#endif // UNIQUE_SOCKET_HPP
