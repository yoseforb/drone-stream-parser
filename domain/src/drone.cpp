#include "drone.hpp"
#include "alert_policy.hpp"
#include "alert_types.hpp"
#include "telemetry.hpp"

#include <set>
#include <string>
#include <utility>
#include <vector>

Drone::Drone(std::string drone_id) : drone_id_(std::move(drone_id)) {}

const std::string& Drone::getId() const noexcept { return drone_id_; }

const std::set<AlertType>& Drone::getAlertState() const noexcept {
  return alert_state_;
}

std::vector<AlertTransition>
Drone::updateFrom(const Telemetry& telemetry,
                  const AlertPolicy& policy) noexcept {
  latitude_ = telemetry.latitude;
  longitude_ = telemetry.longitude;
  altitude_ = telemetry.altitude;
  speed_ = telemetry.speed;
  timestamp_ = telemetry.timestamp;

  std::vector<AlertTransition> transitions;

  auto evaluate = [&](AlertType type, bool triggered) {
    bool const Active = alert_state_.contains(type);
    if (triggered && !Active) {
      alert_state_.insert(type);
      transitions.push_back({type, true});
    } else if (!triggered && Active) {
      alert_state_.erase(type);
      transitions.push_back({type, false});
    }
  };

  evaluate(AlertType::ALTITUDE, telemetry.altitude > policy.altitude_limit);
  evaluate(AlertType::SPEED, telemetry.speed > policy.speed_limit);

  return transitions;
}
