#ifndef TELEMETRY_HPP
#define TELEMETRY_HPP

#include <cstdint>
#include <string>

struct Telemetry {
  std::string drone_id;
  double latitude;
  double longitude;
  double altitude;
  double speed;
  uint64_t timestamp;
};

#endif // TELEMETRY_HPP
