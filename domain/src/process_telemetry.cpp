#include "process_telemetry.hpp"
#include "alert_policy.hpp"
#include "drone.hpp"
#include "i_alert_notifier.hpp"
#include "i_drone_repository.hpp"
#include "telemetry.hpp"
#include <utility>

ProcessTelemetry::ProcessTelemetry(IDroneRepository& repository,
                                   IAlertNotifier& notifier, AlertPolicy policy)
    : repository_(&repository), notifier_(&notifier), policy_(policy) {}

void ProcessTelemetry::execute(const Telemetry& telemetry) {
  auto existing = repository_->findById(telemetry.drone_id);
  Drone drone =
      existing.has_value() ? std::move(*existing) : Drone(telemetry.drone_id);

  auto transitions = drone.updateFrom(telemetry, policy_);
  repository_->save(drone);

  if (!transitions.empty()) {
    notifier_->notify(telemetry.drone_id, transitions);
  }
}
