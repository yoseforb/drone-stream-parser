#include "tcp_server.hpp"

#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/poll.h> // NOLINT(misc-include-cleaner) — POSIX poll
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <spdlog/spdlog.h>

#include "blocking_queue.hpp"
#include "unique_socket.hpp"

namespace {

auto createListeningSocket(uint16_t port) -> UniqueSocket {
  UniqueSocket sfd(socket(AF_INET, SOCK_STREAM, 0));
  if (!sfd) {
    throw std::runtime_error(
        std::string("TcpServer: socket() failed: ") +
        std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  int opt = 1;
  // NOLINTNEXTLINE(misc-include-cleaner)
  if (setsockopt(sfd.get(), SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0) {
    spdlog::warn("TcpServer: setsockopt(SO_REUSEADDR) failed: {}",
                 std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  sockaddr_in addr{};
  addr.sin_family = AF_INET;
  addr.sin_addr.s_addr = INADDR_ANY;
  addr.sin_port = htons(port);

  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  if (bind(sfd.get(), reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) < 0) {
    throw std::runtime_error(
        std::string("TcpServer: bind() failed: ") +
        std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  if (listen(sfd.get(), 5) < 0) { // NOLINT(readability-magic-numbers)
    throw std::runtime_error(
        std::string("TcpServer: listen() failed: ") +
        std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
  }

  spdlog::info("TcpServer: listening on port {}", port);
  return sfd;
}

auto acceptClient(int server_fd, sockaddr_in& client_addr) -> int {
  socklen_t client_len = sizeof(client_addr);
  // NOLINTNEXTLINE(cppcoreguidelines-pro-type-reinterpret-cast)
  return accept(server_fd, reinterpret_cast<sockaddr*>(&client_addr),
                &client_len);
}

} // namespace

TcpServer::TcpServer(uint16_t port, BlockingQueue<std::vector<uint8_t>>& queue,
                     std::atomic<bool>& stop_flag)
    : queue_(queue), stop_flag_(stop_flag),
      server_fd_(createListeningSocket(port)) {}

TcpServer::~TcpServer() = default;

// NOLINTNEXTLINE(readability-function-cognitive-complexity,readability-function-size)
void TcpServer::run() {
  while (!stop_flag_) {
    pollfd pfd{};
    pfd.fd = server_fd_.get();
    pfd.events = POLLIN;

    constexpr int PollTimeoutMs = 200;
    const int PollRet = poll(&pfd, 1, PollTimeoutMs);
    if (PollRet < 0) {
      if (errno == EINTR) {
        continue;
      }
      spdlog::warn("TcpServer: poll() failed: {}",
                   std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
      break;
    }
    if (PollRet == 0) {
      continue;
    }

    sockaddr_in client_addr{};
    const UniqueSocket ClientFd(acceptClient(server_fd_.get(), client_addr));
    if (!ClientFd) {
      if (errno == EINTR || errno == EAGAIN) {
        continue;
      }
      spdlog::warn("TcpServer: accept() failed: {}",
                   std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
      break;
    }

    std::array<char, INET_ADDRSTRLEN> ip_str{};
    inet_ntop(AF_INET, &client_addr.sin_addr, ip_str.data(), ip_str.size());
    spdlog::info("TcpServer: client connected from {}", ip_str.data());

    recvLoop(ClientFd.get());
    spdlog::info("TcpServer: client disconnected");
  }

  server_fd_.reset();
  queue_.close();
  spdlog::info("TcpServer: shut down");
}

void TcpServer::recvLoop(int client_fd) {
  constexpr std::size_t RecvBufSize = 4096;
  std::array<uint8_t, RecvBufSize> buf{};

  while (!stop_flag_) {
    pollfd pfd{};
    pfd.fd = client_fd;
    pfd.events = POLLIN;

    constexpr int PollTimeoutMs = 200;
    const int PollRet = poll(&pfd, 1, PollTimeoutMs);
    if (PollRet < 0) {
      if (errno == EINTR) {
        continue;
      }
      spdlog::warn("TcpServer: recv poll() failed: {}",
                   std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
      return;
    }
    if (PollRet == 0) {
      continue; // timeout — recheck stop_flag_
    }

    // NOLINTNEXTLINE(clang-analyzer-unix.BlockInCriticalSection)
    const ssize_t BytesRead = recv(client_fd, buf.data(), buf.size(), 0);
    if (BytesRead < 0) {
      if (errno == EINTR) {
        continue;
      }
      spdlog::warn("TcpServer: recv() failed: {}",
                   std::strerror(errno)); // NOLINT(concurrency-mt-unsafe)
      return;
    }
    if (BytesRead == 0) {
      return;
    }

    const auto Count = static_cast<std::size_t>(BytesRead);
    std::vector<uint8_t> chunk(Count);
    std::copy_n(buf.begin(), Count, chunk.begin());
    queue_.push(std::move(chunk));
  }
}
