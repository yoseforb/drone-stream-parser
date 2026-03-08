#ifndef PROCESS_TELEMETRY_HPP
#define PROCESS_TELEMETRY_HPP

#include "alert_policy.hpp"
#include "i_alert_notifier.hpp"
#include "i_drone_repository.hpp"
#include "telemetry.hpp"

class ProcessTelemetry {
public:
  ProcessTelemetry(IDroneRepository& repository, IAlertNotifier& notifier,
                   AlertPolicy policy);

  void execute(const Telemetry& telemetry);

private:
  IDroneRepository&
      repository_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  IAlertNotifier&
      notifier_; // NOLINT(cppcoreguidelines-avoid-const-or-ref-data-members)
  AlertPolicy policy_;
};

#endif // PROCESS_TELEMETRY_HPP
