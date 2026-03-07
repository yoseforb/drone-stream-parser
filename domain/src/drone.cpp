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
                  const AlertPolicy& /*policy*/) noexcept {
  // Stub: update state fields but return no transitions.
  latitude_ = telemetry.latitude;
  longitude_ = telemetry.longitude;
  altitude_ = telemetry.altitude;
  speed_ = telemetry.speed;
  timestamp_ = telemetry.timestamp;
  return {};
}
