#ifndef DRONE_HPP
#define DRONE_HPP

#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "telemetry.hpp"

class Drone {
public:
  explicit Drone(std::string drone_id);

  [[nodiscard]] const std::string& getId() const noexcept;
  [[nodiscard]] const std::set<AlertType>& getAlertState() const noexcept;

  std::vector<AlertTransition> updateFrom(const Telemetry& telemetry,
                                          const AlertPolicy& policy) noexcept;

private:
  std::string drone_id_;
  double latitude_{};
  double longitude_{};
  double altitude_{};
  double speed_{};
  uint64_t timestamp_{};
  std::set<AlertType> alert_state_;
};

#endif // DRONE_HPP
