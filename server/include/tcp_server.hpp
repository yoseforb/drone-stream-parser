#ifndef TCP_SERVER_HPP
#define TCP_SERVER_HPP

#include <atomic>
#include <cstdint>
#include <vector>

#include "blocking_queue.hpp"
#include "unique_socket.hpp"

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
  UniqueSocket server_fd_;

  void recvLoop(const UniqueSocket& client_fd);
};

#endif // TCP_SERVER_HPP
