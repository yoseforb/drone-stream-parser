#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "blocking_queue.hpp"

class TcpServer {
public:
  TcpServer(uint16_t port, BlockingQueue<std::vector<uint8_t>>& queue,
            std::atomic<bool>& stop_flag);
  ~TcpServer();

  TcpServer(const TcpServer&) = delete;
  TcpServer& operator=(const TcpServer&) = delete;
  TcpServer(TcpServer&&) = delete;
  TcpServer& operator=(TcpServer&&) = delete;

  void run();

private:
  BlockingQueue<std::vector<uint8_t>>& queue_;
  std::atomic<bool>& stop_flag_;
  int server_fd_{-1};

  void recvLoop(int client_fd);
};

#endif // TCP_SERVER_HPP
