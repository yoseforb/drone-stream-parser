#include "process_telemetry.hpp"
#include "alert_policy.hpp"
#include "i_alert_notifier.hpp"
#include "i_drone_repository.hpp"
#include "telemetry.hpp"

ProcessTelemetry::ProcessTelemetry(IDroneRepository& repository,
                                   IAlertNotifier& notifier, AlertPolicy policy)
    : repository_(&repository), notifier_(&notifier), policy_(policy) {}

void ProcessTelemetry::execute(const Telemetry& telemetry) {
  // Stub: will implement use case logic in a later step.
  // Touch members to avoid unused-private-field warnings.
  static_cast<void>(repository_);
  static_cast<void>(notifier_);
  static_cast<void>(policy_);
  static_cast<void>(telemetry);
}
