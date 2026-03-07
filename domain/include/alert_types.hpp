#ifndef ALERT_TYPES_HPP
#define ALERT_TYPES_HPP

#include <cstdint>

enum class AlertType : std::uint8_t {
  ALTITUDE,
  SPEED,
};

struct AlertTransition {
  AlertType type;
  bool entered;
};

#endif // ALERT_TYPES_HPP
